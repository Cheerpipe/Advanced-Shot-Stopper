# Auditoría estática de firmware ESP32/FreeRTOS

**Proyecto:** AcaiaArduinoBLE / Shot Stopper  
**Objetivo:** evaluar la aptitud arquitectónica, temporal y de memoria para un estándar de misión crítica  
**Fecha de corte:** 2026-09-05  
**Configuración verificada:** ESP32-S3 N16R8, ESP-IDF 5.5.5, Arduino como componente, NimBLE  
**Naturaleza del análisis:** auditoría estática, compilación completa y pruebas host. No sustituye pruebas de temporización, estrés RF, fault injection ni certificación sobre hardware.

## 1. Dictamen ejecutivo

El firmware contiene defensas de seguridad superiores a las habituales en un proyecto Arduino: temporizador independiente de corte, política fail-closed del relé, rollback OTA, Task Watchdog con pánico, buffers grandes en PSRAM, colas acotadas, generación de callbacks BLE, comprobaciones de integridad y una batería host extensa. Sin embargo, **no debe clasificarse aún como apto para misión crítica**.

La razón principal no es funcional sino semántica: existen accesos concurrentes que constituyen *data races* bajo el modelo de memoria de C++, estados de seguridad publicados mediante `volatile`, instantáneas tipo seqlock que copian objetos no atómicos durante escrituras concurrentes, y ciclos de vida de tareas/callbacks que no garantizan quiescencia. Una prueba funcional secuencial puede pasar indefinidamente y aun así el compilador estar autorizado a optimizar esos accesos de forma inesperada.

### Bloqueadores de liberación

1. El worker de balanza modifica y consulta estado propiedad de la tarea de control sin un protocolo de transferencia de propiedad.
2. Las instantáneas `ControlStatus`, `ControlGate`, receta y profiler usan un seqlock que no elimina la carrera C++ sobre el payload y, tras 64 intentos, acepta una copia potencialmente rasgada.
3. Flags de fallo/reinicio y estado del GPTimer compartido entre tarea e ISR usan `volatile` como sincronización.
4. El fallo al crear la tarea de persistencia deja una cola huérfana que acepta datos y bloquea permanentemente la durabilidad.
5. El estado `BOOT_READY` omite dependencias de seguridad que sí son necesarias para operar.
6. El singleton de callbacks NimBLE no garantiza vida útil ni sincroniza consistentemente los campos compartidos.

**Decisión recomendada:** congelar la promoción a producción crítica hasta completar la Fase 0 y sus pruebas de aceptación. La Fase 1 debe cerrarse antes de una liberación de campo que permita OTA o control remoto.

## 2. Alcance, método y evidencia

### 2.1 Superficie revisada

- Aproximadamente 43.500 líneas de código de producción C/C++ propio, excluyendo assets generados y pruebas.
- Unidades principales: `src/shotStopper.cpp`, `ShotStopperNetwork.cpp`, `ShotStopperScaleWorker.cpp`, `ShotStopperDomain.h`, `ShotStopperMachineRelay.h`, `ShotStopperOta.*`, `ShotStopperWebhook.*`, `ShotStopperHardwareTimer.h` y la implementación NimBLE de `libraries/EspressoScaleBLE`.
- Configuración IDF, scripts de compilación/análisis, particiones, afinidad y asignadores.
- Rutas de ISR, temporizadores, colas, mutexes, spinlocks, callbacks BLE/HTTP y persistencia NVS/flash.

### 2.2 Verificaciones ejecutadas

- Compilación completa `n16r8` con ESP-IDF 5.5.5: **correcta**.
- Suite host de la librería BLE: **17.043 checks portables**, **31 de advertising** y **32.049 de resiliencia**, todos correctos.
- Suite host del firmware: suites principal, momentary, persistence, safety, remote lockdown, OTA, BLE companion, JSON y Web UI, todas correctas.
- `cppcheck`: sin diagnósticos en las 20 unidades que actualmente cubre el script.
- Analizador estático de GCC: sin diagnósticos.
- Inspección del mapa ELF para ubicación de BSS/DRAM/IRAM/PSRAM.

Estos resultados demuestran una base funcional fuerte, pero **no refutan carreras**: los stubs host son esencialmente secuenciales, GCC Analyzer no es detector de carreras y el script de `cppcheck` omite tres unidades propias: `NimbleAdvertisement.cpp`, `NimbleResilience.cpp` y `ShotStopperBleCompanionNimble.cpp`.

### 2.3 Escala de prioridad

| Prioridad | Significado | Política propuesta |
|---|---|---|
| P0 crítica | Puede invalidar una decisión de seguridad, causar UB concurrente, pérdida silenciosa de durabilidad o uso después de liberar | Bloquea liberación |
| P1 alta | Puede causar indisponibilidad, watchdog, corrupción diagnóstica importante o degradación acumulativa | Corregir antes de producción de campo |
| P2 media | Deuda con impacto en mantenibilidad, rendimiento o capacidad de diagnóstico | Cerrar en endurecimiento |
| P3 baja | Limpieza, coherencia o defensa futura | Incorporar al mantenimiento normal |

### 2.4 Línea base y matriz de trazabilidad de cierre

Esta matriz separa una corrección presente en el árbol de la evidencia de
ejecución. La línea base del cierre es `b7ed7eb9a0fad9e3236c3225c10235efa8f56765`
(2026-09-06), ESP32-S3 N16R8, ESP-IDF 5.5.5, Arduino como componente y
`idf/sdkconfig.defaults.n16r8`. El build de liberación debe registrar además
el `sdkconfig` generado, hash ELF y versión de toolchain: esos artefactos no
pueden inferirse de esta auditoría estática.

| Hallazgo | Código / símbolo | Prueba host prevista | Sanitizer | HIL | Estado final actual |
|---|---|---|---|---|---|
| F-01 | `ScaleWorkerBridgeCallbacks`, `publishScaleWorkerPolicy` | `scale_worker_concurrency_host_test` | TSAN | matriz BLE/control | pendiente |
| F-02 | snapshots bajo `TaskLockGuard` | `shot_stopper_host_test M09` | TSAN | carga combinada | implementación; HIL pendiente |
| F-03 | `SafetyEventFlags` | `shot_stopper_host_test F03` | TSAN | fault de cada productor | implementación; HIL pendiente |
| F-04 | `IndependentSafetyTimer` | `shot_stopper_host_test F04` | TSAN | borde arm/stop/ISR | implementación; HIL pendiente |
| F-05 | persistencia transaccional | `persistence_host_test` | ASan/UBSan | fallo de creación | implementación; HIL pendiente |
| F-06 | `BootCapability` / estado de arranque | `shot_stopper_host_test` | ASan/UBSan | matriz de dependencias | implementación; HIL pendiente |
| F-07 | lease/generación NimBLE | `nimble_lifecycle_host_test` | TSAN | teardown BLE por fase | pendiente |
| F-08 | cola `serial_log` | suite host | ASan/UBSan | soak de USB | implementación; HIL pendiente |
| F-09 | `ShotStopperOta::publishedState` | `ota_state_concurrency_host_test` | TSAN | OTA interrumpida | implementación; HIL pendiente |
| F-10 | journal OTA / `FlashIoGuard` | `ota_image_host_test` | ASan/UBSan | OTA/abort/flash | implementación; HIL pendiente |
| F-11 | `parseJsonDocument` | `json_arena_host_test` | TSAN | presión HTTP | implementado/verificado en host; HIL pendiente |
| F-12 | locks de tarea / `CONCURRENCY.md` | suite host | TSAN aplicable | traza locks | implementación; HIL pendiente |
| F-13 | `ShotStopperScheduling.h` | `p2_soak.py --self-test` | n/a | 8 h | implementación; HIL pendiente |
| F-14 | buzzer, OTA, NimBLE, NVS | `critical_return_host_test` | ASan/UBSan | fault injection | pendiente |
| F-15 | `begin/stop` Network/Webhook | `webhook_error_host_test` | ASan/UBSan | restart de servicio | implementación; HIL pendiente |
| F-16 | registro RTC de una TU | `reset_guard_instance_host_test` | ASan/UBSan | reinicio | implementación; HIL pendiente |
| F-17 | snapshots de telemetría | `shot_stopper_host_test F17` | TSAN | soak | implementación; HIL pendiente |
| F-18 | telemetría de recursos | `p2_soak.py --self-test` | n/a | 72 h | implementación; HIL pendiente |
| F-19 | ownership de handles | `resource_owner_host_test` | ASan/UBSan | ciclos de servicio | implementación; HIL pendiente |
| F-20 | límites en `check_architecture.py` | `check_architecture.py` | n/a | revisión de build | verificado en host; build target pendiente |
| F-21 | manifiesto TU/análisis | `audit_compile_commands.py` | analizadores | traza objetivo | pendiente |
| F-22 | gates de símbolos muertos | `check_web_assets.js` | ASan/UBSan aplicable | soak | implementación; HIL pendiente |

Ninguna fila con HIL pendiente equivale a una calificación de misión crítica.

La “confianza” expresa certeza del hallazgo estático, no probabilidad de ocurrencia en campo.

## 3. Modelo actual de ejecución y memoria

### 3.1 Tareas y afinidad

| Ejecutor | Núcleo | Prioridad | Stack | Observación |
|---|---:|---:|---:|---|
| Arduino `loop` / control | 1 | prioridad Arduino | 8192 B | Orquestador de dominio; ciclo nominal de 1 ms |
| `scale_worker` | 1 | `idle + 1` | 6656 B | BLE de balanza y publicación de eventos; ciclo de 1 ms conectado |
| `settings_persist` | 1 | `idle` | 4096 B | Escritura de ajustes; suscrita al TWDT |
| `network_manager` | 0 | `idle + 1` | 10240 B | Wi-Fi, estado de red, OTA y coordinación HTTP |
| `webhook` | 0 | `idle` | 4096 B | Cliente HTTP saliente |
| NimBLE host | 0 | configuración IDF | 4096 B | Stack BLE y callbacks |
| servidor `httpd` | afinidad por defecto IDF | `idle + 1` | 8192 B | Un único task de servidor |

La partición es coherente: control y balanza en APP_CPU; Wi-Fi/NimBLE/red en PRO_CPU. No se recomienda mover mecánicamente `scale_worker` al núcleo 0, ya cargado por radio y red. Sí falta un análisis formal de respuesta temporal: control y balanza compiten a igual prioridad y ambos pueden despertar cada milisegundo a 80 MHz; persistencia queda a prioridad idle.

### 3.2 Configuración de seguridad y PSRAM verificada

- FreeRTOS a 1000 Hz, SMP de dos núcleos.
- TWDT de 5 s, pánico habilitado y vigilancia de ambos idle tasks.
- Interrupt Watchdog y watchdog/rollback de arranque habilitados.
- Handlers GPTimer configurados para IRAM.
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` y reserva interna de 32768 B.
- BSS externo habilitado y asignador NimBLE configurado para memoria externa.
- Los stacks de tarea permanecen en memoria interna, decisión correcta para predictibilidad.
- `allocExternal()` exige `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` y no degrada silenciosamente a DRAM.

El mapa enlazado confirma en PSRAM, entre otros, `persistedSettings` (~2,6 KiB), requests de settings (~2,6 KiB), presets (~836 B), shot log (~5,8 KiB), curvas (~10,6 KiB) y debug log (~14,6 KiB). Permanecen en memoria interna el objeto de balanza (~3 KiB), `ControlStatus` (~1 KiB), buzzer (~1,1 KiB), estado del relé y temporizadores. Esta distribución es sensata.

## 4. Hallazgos detallados

## P0 — Críticos / bloqueadores

### F-01. Violación del propietario único entre `scale_worker` y control

**Vectores:** concurrencia, multicore, arquitectura  
**Confianza:** alta  
**Evidencia:** `ShotStopperScaleWorker.cpp:348-402`, `:457`, `:781-862`, `:1023-1025`; `shotStopper.cpp:1793-1797`, `:1226`, `:2452`, `:6366`.

`setScaleLinkState()` se ejecuta en el worker de balanza y, fuera de `scaleLinkMux`, lee `runtimeConfig`, llama `emitAlert()` y ejecuta `resetCupPresence()`. Estos últimos consumen o modifican objetos que pertenecen al loop de control (`session`, buzzer y `cupPresence`). El mismo worker consulta directamente preferencias de Bookoo y `scaleMacCacheMode` desde `runtimeConfig`, mientras control puede publicar/aplicar una configuración nueva.

Además:

- `firmwareInitializationComplete` se lee dentro de `scaleLinkMux`, pero su escritor no toma ese lock. Tomar un lock solamente del lado lector no establece *happens-before*.
- `scaleProtocolName` se escribe bajo `scaleLinkMux`, pero existen lecturas directas desde control sin ese lock.
- Métricas y handles del worker se escriben y leen desde distintas tareas sin un contrato uniforme.

**Modo de fallo:** alerta con configuración mezclada; buzzer manipulado simultáneamente; reset de presencia de taza intercalado con la máquina de estados; lectura de nombre de protocolo parcialmente actualizado; comportamiento indefinido por data race.

**Corrección:** convertir todo cambio de enlace en un evento inmutable `ScaleLinkChanged` enviado a control. Solo control debe emitir alertas y mutar `cupPresence`. Publicar al worker una configuración compacta e inmutable mediante mailbox/doble buffer seguro, con versión. El estado y métricas de balanza deben salir exclusivamente por `ScaleLinkSnapshot` o atómicos escalares.

**Aceptación:** ThreadSanitizer sobre un harness POSIX no detecta carreras al cambiar configuración mientras se simulan connect/disconnect/notificaciones; ninguna función del worker accede directamente a objetos del dominio de control; prueba de propiedad automatizada o revisión de dependencias.

### F-02. Seqlocks formalmente inseguros y fallback con copia rasgada

**Estado de remediación (2026-09-05): corregido.** Los cuatro dominios usan
ahora mutexes de tarea estáticos (`SemaphoreHandle_t` con herencia de prioridad
en ESP32 y `std::mutex` en host). Se eliminaron los contadores de secuencia, los
reintentos acotados y el fallback que copiaba el payload activo. La prueba host
M09 ejecuta un escritor continuo y dos lectores, comprueba invariantes de
`ControlStatus`, `ControlGate`, receta y profiler, y forma parte de la ejecución
con ThreadSanitizer. Si el refresh de status agota su plazo, se devuelve
exclusivamente la última copia completa con `snapshotStale=true`.

**Vectores:** concurrencia, memoria, resiliencia  
**Confianza:** alta  
**Evidencia:** `shotStopper.cpp:942-971`, `:1313-1355`, `:5166-5415`; `ShotStopperTaskProfiler.h:181-195`.

Los contadores de secuencia son atómicos, pero los payloads (`publishedControlStatus`, `publishedControlGate`, `publishedRuntimeConfig`, `publishedPresetBank` y profiler) no lo son. Leer un objeto ordinario mientras otra tarea lo escribe constituye una carrera C++, aunque después se compruebe que cambió el contador. Un seqlock clásico requiere semántica especial de acceso que el estándar C++ no concede a copias ordinarias de structs.

Tras 64 intentos, las funciones realizan de todos modos `output = published...`; esa ruta acepta explícitamente una imagen activa y potencialmente rasgada. En `ControlGate` no es solo telemetría: el resultado participa en autorización de mantenimiento/OTA/configuración.

**Modo de fallo:** combinación imposible de flags e IDs, autorización con un lease antiguo y estado nuevo, serialización de configuración inconsistente, lectura fuera de límites si un campo de longitud se rasga, o optimizaciones no intuitivas por UB.

**Corrección:**

- Codificar `ControlGate` en una única palabra atómica si sus bits/ID caben; alternativamente protegerla con mutex breve.
- Para status/receta: triple buffer con publicación atómica del índice y pin de lectores por slot, RCU con periodo de gracia, o mensajes a un dueño único.
- El fallback debe devolver la última instantánea válida y marcar `stale=true`; nunca copiar el buffer activo.
- Para profiler, usar contadores atómicos o consolidación por la tarea propietaria.

**Aceptación:** prueba concurrente TSAN con escritor continuo y múltiples lectores; invariantes de snapshot nunca se violan; cero fallback rasgado; política explícita para snapshot stale.

### F-03. `volatile` usado como sincronización de fallos de seguridad

**Vectores:** concurrencia, multicore, resiliencia  
**Confianza:** alta  
**Evidencia:** `shotStopper.cpp:563-570`, `:975-986`, `:6468-6485`; `ShotStopperWatchdog.h:60-74`; consumidores en `ShotStopperMachineRelay.h:260-269`, `:414-416`.

`criticalTaskWatchdogFault`, `safeRestartRequested` y `taskWatchdogRestoreFailed` son escritos por tareas/callbacks diferentes y leídos por control. `volatile` impide ciertas eliminaciones de acceso, pero no hace la operación atómica ni crea orden de memoria entre núcleos.

Los flags de feedback declarados `volatile` son menos problemáticos porque sus accesos actuales están bajo el mismo `relayMux`; allí `volatile` es redundante. No debe generalizarse ese patrón.

**Modo de fallo:** petición de reinicio perdida, evaluación inconsistente del estado de fallo, o comportamiento indefinido justo en la ruta fail-safe.

**Corrección:** `std::atomic<bool>` con `store(memory_order_release)`, `load/exchange(memory_order_acquire/acq_rel)` y `static_assert(is_always_lock_free)` para toda variable tocada desde rutas críticas. Otra opción es una notificación/event-group FreeRTOS para tareas; para ISR usar exclusivamente su variante `FromISR`. Los faults deben ser monotónicos hasta que el dueño los consuma de forma explícita.

**Aceptación:** test concurrente de set/exchange sin pérdidas; inspección de ensamblador confirma operación lock-free y residente donde exige la ISR; fault injection desde cada tarea provoca corte/reinicio determinista.

### F-04. Estado GPTimer compartido tarea/ISR sin sincronización válida

**Vectores:** ISR, concurrencia, IRAM  
**Confianza:** alta  
**Evidencia:** `ShotStopperHardwareTimer.h:65-110`, `:133-151`.

`running_` y `ready_` son `volatile bool`. `arm()`/`stop()` escriben desde tarea y `alarmCallback()` escribe `running_` desde ISR. No existe un atómico ni un `portMUX` común. El comentario de “publicar RUNNING antes de start” describe la intención temporal, pero `volatile` no implementa una publicación entre contextos.

El callback ISR, en cambio, está correctamente anotado `IRAM_ATTR`, no llama APIs FreeRTOS y retorna `false`; por ello no falta una macro `FromISR` en esta ruta.

**Modo de fallo:** `running()` obsoleto, rearmado/stop que pisa el disparo, doble transición lógica o aceptación de un temporizador que ya venció.

**Corrección:** estado entero lock-free ubicado en DRAM interna y actualizado con atómicos, o sección crítica ISR/tarea mediante el mismo `portMUX_TYPE`. Callback y contexto deben quedar inmutables mientras el timer esté habilitado. Comprobar todos los retornos de stop/start y distinguir `ESP_ERR_INVALID_STATE` esperado de fallos reales.

**Aceptación:** estrés de arm/stop contra expiraciones a borde; GPIO/trace demuestra una única llamada por armado; TSAN del modelo host; símbolos y camino completo de ISR verificados en IRAM/DRAM interna.

### F-05. Cola de persistencia huérfana si falla la creación de tarea

**Vectores:** memoria, resiliencia, manejo de errores  
**Confianza:** alta  
**Evidencia:** `shotStopper.cpp:6295-6307`, `:4272-4303`.

Se crea `settingsPersistQueue` antes de `settings_persist`. Si `xTaskCreatePinnedToCore()` falla, el código anula el handle, pero conserva la cola. `dispatchSettingsPersist()` solo comprueba la cola y `settingsPersistInFlight`; acepta el request y fija `inFlight=true`. Nadie consumirá la cola y la persistencia queda bloqueada permanentemente, sin reparación ni rechazo explícito.

**Modo de fallo:** el usuario recibe comportamiento aparentemente normal, pero los cambios no sobreviven un reinicio; además queda memoria de cola asignada.

**Corrección:** inicialización transaccional con RAII/scope guard: si falla la tarea, borrar y anular la cola. Publicar una capability `settingsPersistenceReady`; `dispatch` debe exigir cola **y** tarea viva. Considerar esta capacidad obligatoria para admitir mutaciones persistentes.

**Aceptación:** fault injection de cada asignación/task-create; no quedan handles/colas; la API rechaza cambios con error observable; reinicio conserva cada ajuste aceptado.

### F-06. `BOOT_READY` no representa disponibilidad de los mecanismos obligatorios

**Vectores:** resiliencia, arquitectura  
**Confianza:** alta  
**Evidencia:** inicialización en `shotStopper.cpp:6104-6225`; decisión en `:6364-6373`.

`bootDegraded` considera persistencia cargada, worker de balanza, cola web y red. No incluye, entre otros, reloj de plataforma, timers de seguridad del relé, TWDT, task de persistencia ni un fault crítico ya detectado. Algunas fallas impiden cerrar el relé gracias al chequeo fail-closed de `machineSetClosed()`, lo cual es positivo, pero el sistema aún anuncia `BOOT_READY`/`firmwareInitializationComplete`.

**Modo de fallo:** estado operativo falso, automatización externa que interpreta salud donde solo existe seguridad pasiva, y diagnóstico ambiguo.

**Corrección:** matriz tipada de capacidades (`MandatorySafety`, `MandatoryDurability`, `OptionalConnectivity`). Estados explícitos `BOOTING`, `READY`, `DEGRADED_SAFE`, `FAULT_LATCHED`. Solo `READY` cuando todos los requisitos de la política operativa están presentes. No ligar “red no disponible” con “firmware no inicializado” si la red es opcional; distinguir ambas dimensiones.

**Aceptación:** tabla de fault injection que falla una dependencia por vez y verifica estado, permiso de accionar relé, códigos de diagnóstico y recuperación.

### F-07. Vida útil y campos de callback NimBLE no seguros

**Vectores:** concurrencia, multicore, memoria  
**Confianza:** alta  
**Evidencia:** `EspressoScaleBLENimble.cpp:163-177`, `:564-612`, `:781-828`, `:1736-1811`.

`activeClient_` es un puntero estático ordinario asignado/desasignado desde el dueño y leído por callbacks NimBLE. No existe sincronización ni espera a que terminen callbacks ya despachados antes del destructor. Convertir solo el puntero a atómico impediría la carrera escalar, pero no un use-after-free tras el `load`.

`onNotification()` hace una primera lectura sin lock de `connectionHandle_` y `readHandle_`, y luego repite la validación bajo `mux_`. `finishLink()` y otros caminos escriben esos handles fuera del mutex. Un lock solo del lector no sincroniza con el escritor.

**Modo de fallo:** callback sobre objeto destruido, aceptación/rechazo incorrecto de un paquete perteneciente a otra generación, limpieza concurrente de colas/handles o corrupción de estado BLE.

**Corrección:** registrar un contexto con token de vida/generación; impedir nuevos callbacks, cancelar operaciones, esperar quiescencia/refcount cero y recién destruir. Todos los campos callback-shared deben mutarse bajo el mismo lock o enviarse como mensajes inmutables al task dueño. Mantener la generación, que es una defensa útil, pero no usarla como sustituto de vida útil.

**Aceptación:** pruebas de disconnect/destructor durante cada fase GATT y durante notification flood; TSAN limpio; contador de callbacks activos llega a cero antes de liberar; callbacks tardíos se descartan sin tocar el objeto.

## P1 — Altos

**Estado del plan P1 (2026-09-06): implementación completada en los puntos
cerrados; la calificación HIL continúa pendiente.**

| Requisito | Estado | Evidencia de cierre |
|---|---|---|
| F-08 Logging no bloqueante y sincronizado | ✅ Completado | Flag atómico de criticidad, cola serial acotada con drops y exportación lineal por chunks |
| F-09 Estado OTA sincronizado | ✅ Completado | Mutex de transición, estado publicado y flags de restart atómicos; prueba concurrente TSAN |
| F-10 OTA acotada y coordinada con flash | ✅ Completado | SHA incremental, journal cada 512 KiB, coordinador flash/NVS y sin ampliación global del TWDT |
| F-11 JSON reentrante | ✅ Host verificado | Documentos cJSON independientes, sin hooks/arena globales; límites de bytes/nodos/profundidad y prueba concurrente TSAN |
| F-12 Spinlocks reducidos y orden documentado | ✅ Completado | Mutexes task-only, trabajo RTC fuera del lock ISR y DAG en `docs/CONCURRENCY.md` |
| F-13 Contrato de schedulability | ✅ Completado | Tabla versionada y telemetría de deadlines; procedimiento HIL de 8 h en `docs/SCHEDULABILITY.md` |
| F-14 Retornos críticos manejados | 🟡 En cierre | Fallo/rollback en HTTP, Wi-Fi, RF y relé; quedan inventario completo y fault injection NVS/OTA |
| F-15 Inicio/parada transaccional | ✅ Completado | `begin/stop` reversibles con stop/ack/join para Network y Webhook; fault injection host |
| F-16 Única instancia RTC | ✅ Completado | Definición en una sola TU y prueba multi-TU |
| F-17 Telemetría coherente | ✅ Completado | Contadores atómicos, snapshot con versión/timestamp y prueba multiwriter/reader TSAN |

La ejecución HIL prolongada definida para F-08, F-10 y F-13 permanece como
validación de release sobre hardware físico; no se declara ejecutada por esta
remediación de software.

### F-08. Logging consulta estado de control sin sincronización y puede bloquear tareas vigiladas

**Estado de remediación (2026-09-06): ✅ corregido.** Se publica la criticidad
de control mediante un atómico lock-free; el sink de `esp_log` encola en tiempo
cero hacia una tarea serial dedicada y contabiliza pérdidas. El ring se copia
linealmente una sola vez y el CLI emite el snapshot en chunks fuera del camino
crítico.

**Vectores:** concurrencia, TWDT, rendimiento  
**Confianza:** alta  
**Evidencia:** `shotStopper.cpp:789`, `:909`, `:5722-5744`; `ShotStopperDomain.h:2462+`.

Callbacks desde red/balanza consultan directamente `session.active` y `circuitClosed` para decidir si escribir por Serial. Ambos pertenecen a otros dominios de sincronización. Además, Serial/`esp_log` puede bloquear si el consumidor no drena. El dump CLI puede emitir alrededor de 15 KiB desde el loop de control y buscar repetidamente el siguiente registro en el ring, con coste cuadrático.

**Corrección:** publicar un único `controlCritical` atómico o incluirlo en un snapshot seguro. Enviar logs a una tarea dedicada mediante cola acotada y política de drop/timeout. Exportar el ring por snapshot lineal, en chunks, fuera del loop crítico.

**Aceptación:** desconectar el host serie durante carga máxima no dispara TWDT ni eleva el peor periodo de control sobre el presupuesto; log flood tiene pérdidas contabilizadas, nunca bloqueo no acotado.

### F-09. Estado OTA compartido sin dueño único

**Estado de remediación (2026-09-06): ✅ corregido.** Todas las transiciones y
snapshots OTA están serializados por un mutex de tarea; las consultas frecuentes
usan estado publicado atómicamente y el reinicio pendiente usa atómicos. El
harness concurrente cubre upload/snapshot/cancel/service bajo TSAN.

**Vectores:** concurrencia, resiliencia  
**Confianza:** alta  
**Evidencia:** handlers en `ShotStopperNetwork.cpp:8261-8448`; servicio concurrente en `:8451+`; `otaRestartPending_` en `ShotStopperNetwork.h:286`; estado mutable de `ShotStopperOta.h`.

El task HTTP ejecuta `create/write/snapshot/commit/discard`, mientras `network_manager` consulta `busy`, aplica power-save, confirma/rechaza rollback y lee estado. Los campos de `ShotStopperOta` son ordinarios. `otaRestartPending_` y su timestamp se escriben en HTTP y se leen/limpian en network sin lock.

**Modo de fallo:** commit/confirm/discard intercalados, estado de sesión rasgado, doble transición de restart o decisión de power-save inconsistente.

**Corrección:** un único dueño OTA con command queue y respuestas correlacionadas. Alternativa transitoria: mutex que cubra **toda** transición y snapshot, más atómicos para flags independientes.

**Aceptación:** upload, cancel, confirmación y consulta simultáneos bajo TSAN/fault injection; máquina OTA rechaza comandos inválidos por estado y jamás ejecuta dos operaciones de partición concurrentes.

### F-10. Persistencia OTA con amplificación de lectura/escritura y degradación global del TWDT

**Estado de remediación (2026-09-06): ✅ corregido en software.** El SHA se mantiene de
forma incremental, los checkpoints del journal se espaciaron a 512 KiB y toda
operación de partición/NVS usa el coordinador de flash. Se eliminó la ampliación
global del TWDT; los bucles largos ceden CPU y alimentan el watchdog local.

**Vectores:** TWDT, flash, rendimiento, errores  
**Confianza:** alta  
**Evidencia:** `ShotStopperOta.cpp`; journal de 512 KiB, contadores de fallo
de abort/journal y watchdog global configurado una sola vez.

Cada checkpoint vuelve a leer y hashear todo el prefijo de la imagen. Con una imagen de 3 MiB y checkpoints de 64 KiB, el patrón suma aproximadamente 73,5 MiB de relectura, además del hash final y verificación de `esp_ota_end`; también genera cerca de 48 actualizaciones de journal NVS. La restauración en boot y verificación recorren el prefijo sin un yield/feed explícito.

El guard temporal amplía globalmente el TWDT a 30 s, debilitando la detección de inanición para todas las tareas, y el llamador no comprueba que `widened()` haya tenido éxito antes de continuar. El journal OTA usa `Preferences` fuera del coordinador/mutex común de flash.

**Corrección:** hash incremental recuperable o checkpoints más espaciados con presupuesto de desgaste; serializar toda NVS/flash bajo el coordinador; trocear operaciones con yield/feed medido; si no puede establecerse la ventana requerida, rechazar la operación. Preferir desuscribir solo la tarea que ejecuta una operación conocida y acotada antes que relajar globalmente todos los deadlines, si la política IDF lo permite.

**Aceptación:** WCET medido en flash real, contador de escrituras por actualización, reboot en cada frontera de checkpoint, brownout/fallo de NVS, y ninguna ventana global sin auditoría.

### F-11. Hooks globales de cJSON con arena global no reentrante

**Estado de remediación (2026-09-06): ✅ corregido.** Se eliminaron los hooks y
el bump allocator global: cada parseo crea y destruye su documento cJSON con
límites explícitos de payload, nodos y profundidad. La prueba concurrente TSAN
verifica documentos independientes, JSON inválido y agotamiento acotado.

**Vectores:** memoria, concurrencia, arquitectura  
**Confianza:** alta como deuda; ocurrencia actual mitigada  
**Evidencia:** `ShotStopperJsonArena.h:24-116`.

Los hooks de cJSON son globales al proceso y apuntan a un bump allocator cuyo puntero/reset no está protegido. Hoy el uso propio se concentra en el task HTTP único, lo que reduce la probabilidad, pero cualquier componente futuro o dependencia que use cJSON en otro task puede solapar parseos o resetear memoria todavía viva.

**Corrección:** parser con arena/contexto pasado por el llamador, o parser de tokens estático como jsmn. Un mutex global documentado es solo mitigación temporal y debe incluir todo el ciclo parse-use-delete.

**Aceptación:** dos parseos concurrentes no comparten memoria; tests de profundidad, tamaño máximo, JSON inválido y agotamiento retornan error sin tocar estado previo.

### F-12. Abuso de spinlocks para estado de tarea y trabajo O(N)

**Estado de remediación (2026-09-06): ✅ corregido.** Los dominios task-only de
Network, Webhook, scale preferences y logging usan mutexes con herencia de
prioridad; la copia/búsqueda del ring y el bookkeeping RTC se ejecutan fuera de
secciones ISR. El orden global y las prohibiciones de anidamiento quedaron
documentados en `docs/CONCURRENCY.md`.

**Vectores:** ISR, inversión de prioridades, rendimiento  
**Confianza:** alta  
**Evidencia:** 296 entradas `portENTER_CRITICAL`; `ShotStopperDomain.h:2462+`; `ShotStopperMachineRelay.h:31-55`, `:329-384`; secciones en network/scale/NimBLE.

Los `portMUX` son apropiados para datos ISR compartidos y operaciones escalares mínimas. Aquí también protegen copias, `memset`, validaciones y búsquedas en buffers PSRAM. Eso deshabilita interrupciones y no ofrece herencia de prioridad.

Casos relevantes:

- `debugLog.copyFirstAfter()` recorre el ring bajo spinlock y se invoca repetidamente por evento; los escritores usan try-lock y descartan, evitando bloqueo del control pero aumentando pérdida diagnóstica.
- La ruta del relé ejecuta bajo `relayMux` actualización/checksum de historial RTC, `digitalWrite()` y consultas temporales. La acción eléctrica se realiza temprano, lo cual es correcto, pero el ISR independiente usa el mismo mux y su latencia máxima no está demostrada.
- `onAdvertisement()` mantiene `advertMux_` y toma `mux_` anidado. No se encontró el orden inverso actual, por lo que no se afirma un deadlock presente; el orden no está documentado y es frágil.

**Corrección:** reservar spinlocks para bloques constantes de pocas instrucciones en DRAM interna; usar mutex con herencia o dueño único para task-only; extraer copias/búsquedas fuera del lock mediante snapshots; documentar un DAG de locks. En relé, precomputar bookkeeping y mantener bajo lock solo el GPIO/flags indispensables.

**Aceptación:** instrumentación GPIO/SystemView del tiempo máximo con interrupciones deshabilitadas; límite explícito y assertion/telemetría; análisis automático/manual de orden de locks.

### F-13. No existe demostración de schedulability por núcleo

**Estado de remediación (2026-09-06): ✅ corregido en firmware.** Se añadió una
tabla contractual de núcleo, prioridad, periodo, deadline, stack y bloqueo; la
persistencia dejó prioridad idle. Control y scale worker publican máximo gap y
misses monotónicos en snapshots versionados. `docs/SCHEDULABILITY.md` define el
gate HIL reproducible de 8 horas, cuya ejecución física sigue siendo requisito
de release.

**Vectores:** FreeRTOS, multicore, TWDT  
**Confianza:** alta respecto de la ausencia de evidencia  
**Evidencia:** `shotStopper.cpp:131-132`; `ShotStopperScaleWorker.cpp:412-420`, `:1927-1930`; `ShotStopperNetwork.cpp:1033-1036`, `:3513-3515`; `ShotStopperWebhook.cpp:103-104`.

Control y scale worker comparten núcleo y prioridad efectiva cercana, ambos con periodo activo de 1 ms. Settings persistence, a prioridad idle en ese núcleo, puede ser postergada durante ráfagas; al estar suscrita al TWDT, la consecuencia es reinicio, no solo latencia. En el núcleo 0 coinciden Wi-Fi, NimBLE, network manager, httpd y webhook.

**Corrección:** convertir el worker a event-driven/notificaciones donde sea posible; definir tabla formal de deadline, prioridad, WCET, bloqueo máximo y presupuesto por task. Medir combinaciones adversas: scan Wi-Fi + BLE connect/notify + OTA + webhook + CDC sin lector + escrituras flash.

**Aceptación:** prueba de estrés de varias horas con trazas; `p99.999` y máximo de loop/edad de muestra dentro de presupuesto; ausencia de reset TWDT; stack high-water con margen definido, no solo “mayor que cero”.

### F-14. Manejo incompleto de retornos `esp_err_t` y equivalentes

**Estado de remediación (2026-09-06): 🟡 parcialmente corregido.** Además de
HTTP/Wi-Fi/RF/relé, buzzer verifica sus timers y queda fail-silent, OTA cuenta
abortos/journal fallidos, y NimBLE registra teardown inesperado tras invalidar
su generación. El inventario completo y fault injection de todas las rutas NVS
siguen abiertos; no se declara cerrado por la suite host.

**Vectores:** resiliencia, mantenibilidad  
**Confianza:** alta  
**Evidencia principal:** `ShotStopperWebhook.cpp:449-480`; `ShotStopperNetwork.cpp:1812-1814`; `ShotStopperRfCoex.h:45-51`; `ShotStopperMachineRelay.h:168-187`; `ShotStopperOta.cpp:135-157`, `:349`, `:493`, `:633`; buzzer `:154/:163`; NimBLE `EspressoScaleBLENimble.cpp:1764-1772`.

Ejemplos:

- Webhook ignora retornos de setters de método, headers y body antes de `perform()`.
- `WiFi.STA.connect()` se ignora y el camino retorna éxito de inicio; el timeout posterior recupera, pero se pierde el error inmediato.
- `esp_coex_preference_set()` se ignora y el snapshot declara una preferencia aunque la aplicación haya fallado.
- Relay puede filtrar el primer `esp_timer` si falla la creación del segundo, y ambos si falla el temporizador independiente.
- Se ignoran abort/stop/start de OTA/timers y operaciones `Preferences::remove`.
- NimBLE ignora cancel/terminate; la generación mitiga callbacks viejos, pero no confirma que el controlador haya terminado.

**Corrección:** wrappers `Result<T>`/`ESP_RETURN_ON_ERROR` con política por subsistema: retry acotado, degradación explícita o latch fatal. Preservar el primer error y añadir contadores/último código al diagnóstico. Solo ignorar un retorno con comentario y lista explícita de códigos aceptables.

**Aceptación:** fault injection para cada API crítica; cobertura de ramas de error; cero cast `(void)` sobre APIs críticas sin justificación revisada.

### F-15. Inicialización de Network/Webhook no es transaccional

**Estado de remediación (2026-09-06): ✅ corregido.** Network y Webhook poseen
`stop()` idempotente, protocolo stop/ack/join y rollback inverso de recursos;
los tasks arrancan al final de la adquisición. Un fallo parcial retorna al
baseline y permite un segundo `begin()` definido.

**Vectores:** memoria, tareas, resiliencia  
**Confianza:** alta  
**Evidencia:** `ShotStopperNetwork.cpp:985-1048`; `ShotStopperWebhook.cpp:34-111`.

`ShotStopperNetwork::begin()` inicializa `webhooks_` antes de terminar la asignación de cola, mutexes, work buffer y task principal. Un fallo posterior retorna `false` y limpia solo parte de los recursos; no existe un shutdown/join equivalente para revertir el webhook. Si estaba habilitado, puede quedar un task huérfano; incluso deshabilitado puede quedar su mutex de ciclo de vida.

**Corrección:** adquirir primero recursos pasivos, arrancar tasks al final y usar scope guard para rollback inverso. Implementar `stop()` idempotente: cancelar I/O, señalizar, join/ack, borrar task/colas/mutexes y liberar buffers en un orden probado.

**Aceptación:** fault injection después de cada paso de `begin`; heap y número de tasks retornan al baseline; segundo `begin` funciona o es rechazado de forma definida.

### F-16. Estado RTC definido `static` en header produce múltiples registros

**Estado de remediación (2026-09-06): ✅ corregido.** El registro RTC y su reloj
de checkpoint tienen una única definición en `ShotStopperResetGuard.cpp`; el
header solo declara la instancia. Una prueba multi-TU verifica que todos los
consumidores observan la misma dirección y contenido.

**Vectores:** memoria, seguridad, arquitectura  
**Confianza:** alta  
**Evidencia:** `ShotStopperResetGuard.h:46-51`; mapa ELF.

El header define variables `RTC_NOINIT_ATTR static`, por lo que cada unidad que lo incluye obtiene una copia. El ELF contiene dos registros de 124 B consecutivos en RTC (248 B en total), provenientes de `shotStopper.cpp` y `ShotStopperNetwork.cpp`. Hoy network usa esencialmente helpers de nombre, por lo que la segunda copia parece inactiva; el diseño permite que una llamada futura consulte o modifique “otro” historial de seguridad.

**Corrección:** una única definición en `.cpp` con declaración `extern` y API cerrada. Separar helpers puros que no requieren estado RTC.

**Aceptación:** `nm/map` muestra exactamente una instancia; test de reboot simulado comprueba lectura/escritura desde todos los consumidores.

### F-17. Telemetría de salud también contiene carreras

**Estado de remediación (2026-09-06): ✅ corregido.** Los contadores de PSRAM,
flash y publicación transversal son atómicos o se copian bajo mutex de tarea.
`ControlStatusSnapshot` incorpora versión, timestamp y métricas de deadline; el
harness multiwriter/reader verifica monotonicidad e integridad bajo TSAN.

**Vectores:** concurrencia, diagnóstico  
**Confianza:** alta  
**Evidencia:** `ShotStopperNetwork.cpp:1100-1112`, escritores `:1291-1296`; métricas de scale worker; `ShotStopperPsram.h:33-48`; contadores de flash I/O.

Algunas métricas se copian después de liberar el mutex de status; contadores de asignación, drops, stack mínimo y progreso se comparten como enteros ordinarios. Aunque muchas no accionan directamente el relé, una plataforma crítica debe poder confiar en sus diagnósticos; una alerta de stack falsa o un progreso rasgado puede inducir recuperación incorrecta.

**Corrección:** contadores monotónicos atómicos, o actualización y copia bajo el snapshot dueño. Etiquetar cada métrica como observacional o decisoria; las decisorias requieren la misma integridad que el control.

**Aceptación:** TSAN limpio para productores múltiples; invariantes monotónicas; snapshot lleva timestamp y versión coherentes.

## P2 — Medios / endurecimiento

### F-18. Churn de heap interno y fragmentación a largo plazo

**Estado de remediación (2026-09-06): ✅ implementación corregida; soak HIL
pendiente.** El cliente HTTP y el worker se conservan entre cambios ordinarios
de configuración; diagnóstico publica create/reuse/cleanup, starts/stops y
heap interno/PSRAM antes/después de cada envío. `p2_soak.py` falla ante handles
o reinicios de worker no acotados y tendencias negativas. La evidencia física
8 h/72 h sigue siendo gate de release, no trabajo de implementación omitido.

**Vectores:** memoria, PSRAM, rendimiento  
**Confianza:** media-alta  

No se observó uso problemático de `std::string`, `String` o containers crecientes en hot loops; predominan arrays fijos y buffers preasignados. Es una fortaleza. El riesgo residual proviene de crear/destruir `esp_http_client` en cada webhook, arrancar/parar workers al cambiar configuración y reiniciar estructuras de servidor/red durante recuperación. TLS y HTTP pueden fragmentar heap interno aun si payload y colas viven en PSRAM.

**Corrección:** cliente HTTP persistente/reutilizable cuando la API lo permita; tasks con vida estable y estado idle; instrumentar `heap_caps_get_largest_free_block()` por capability, fallos de alloc y watermark antes/después de cada operación. Mantener buffers pesados en `MALLOC_CAP_SPIRAM`; no trasladar a PSRAM stacks, objetos ISR, DMA ni locks.

**Aceptación:** soak test de decenas de miles de webhooks/reconexiones/OTA abortadas sin tendencia descendente del mayor bloque interno ni aumento no acotado de tasks/handles.

### F-19. RAII incompleto para handles ESP/FreeRTOS

**Estado de remediación (2026-09-06): ✅ corregido.** `UniqueResource` aporta
ownership no asignante, move, `release()` y rollback. Ya posee el cliente HTTP,
handle/SHA OTA y recursos pasivos de Network; los timers de relé usan owners
transaccionales durante construcción. Tasks conservan explícitamente
stop/ack/join. El inventario y excepciones están versionados en
`docs/RESOURCE_OWNERSHIP.md` y cubiertos por fault injection/sanitizers host.

**Vectores:** memoria, mantenibilidad  
**Confianza:** alta  

Los objetos de vida global mitigan fugas en el camino feliz, pero la liberación manual ya produjo fallos parciales reales (timers de relé, cola de persistencia, Network/Webhook). Faltan owners únicos para `esp_timer_handle_t`, `gptimer_handle_t`, `esp_http_client_handle_t`, colas, mutexes y sesiones OTA.

**Corrección:** wrappers mínimos no asignantes con destructor, move y `release()`. Para tasks FreeRTOS, no borrar externamente mientras ejecutan: protocolo stop/ack/join antes de liberar recursos. La RAII debe codificar orden de destrucción, no esconderlo.

**Aceptación:** fault injection de constructores parciales; sanitizers host; conteo de recursos igual al baseline tras rollback.

### F-20. Monolitos y acoplamiento global elevan la complejidad

**Estado de remediación (2026-09-06): ✅ límites implementados.** Los seis
owners de servicio, sus mensajes y excepciones están formalizados en
`docs/ARCHITECTURE.md`. ScaleService ya no incluye ni referencia el singleton
NetworkService: publica RF mediante `ScaleWorkerBridgeCallbacks`. El gate
automático prohíbe dependencias Safety→Network/HTTP/JSON, bypasses de Control y
crecimiento de los tres roots legacy; harnesses independientes prueban cada
dominio sin incluir ambos monolitos.

**Vectores:** arquitectura, mantenibilidad, complejidad ciclomática  
**Confianza:** alta  

`ShotStopperNetwork.cpp` está limitado a 8.750 líneas y aún reúne Wi-Fi, HTTP routing, auth, JSON, serialización, NTP, OTA y comandos de almacenamiento. `shotStopper.cpp` (~6.500 líneas) y `ShotStopperDomain.h` (~3.200) concentran estado y lógica transversal. El ELF muestra funciones muy grandes: el status handler ronda 16 KiB de código máquina, restore del journal OTA ~13 KiB, config handler y procesador de comandos varios KiB. El tamaño máquina está afectado por inlining, pero confirma una superficie difícil de revisar.

Los headers con implementación y los `extern` globales vuelven implícitas las dependencias y frágil el orden de inclusión/ODR.

**Corrección arquitectónica:**

- `SafetyKernel`: relé, timers, watchdog, reset guard; sin red ni JSON.
- `ControlOrchestrator`: única autoridad sobre sesión, taza, receta activa y alertas.
- `ScaleService`: BLE y eventos inmutables; nunca muta dominio.
- `NetworkService`: transporte; convierte requests a comandos y snapshots a respuestas.
- `PersistenceService`: única autoridad de NVS/particiones con agenda y presupuesto.
- `DiagnosticsService`: logging, métricas y exportación desacoplados.

Interfaces por mensajes trivially-copyable, capacidades explícitas y ausencia de referencias globales entre dominios.

**Aceptación:** límites de dependencia comprobables en build; cada módulo testeable sin incluir el monolito; ownership documentado para cada estado mutable.

### F-21. Cobertura estática y concurrente insuficiente

**Estado de remediación (2026-09-06): ✅ implementación corregida; traza target
pendiente.** El manifiesto de producción y `audit_compile_commands.py` obligan
a cubrir 25/25 TUs exactamente una vez antes de Cppcheck/clang-tidy, incluidos
NimBLE y Companion. La suite ejecuta ASan/UBSan y TSAN con threads reales para
snapshots, safety, OTA y JSON; warnings/supresiones tienen baseline, owner y
fecha de revisión. `docs/P2_TARGET_TRACE.md` define escenarios/artefactos HIL.
Cppcheck bloquea `warning`, `performance` y `portability`; `unusedFunction` se
excluye porque la base filtrada y variante-específica clasifica APIs cross-TU
como muertas, y su revisión queda separada bajo F-22 sin supresiones amplias.

**Vectores:** calidad, proceso  
**Confianza:** alta  

El hallazgo original era que `cppcheck` cubría 20 de 23 TUs propios y no
modelaba FreeRTOS. Tampoco había compile database auditada, ThreadSanitizer
concurrente ni un contrato de trazas target sistemáticas. La remediación
anterior eleva la cobertura actual a 25/25 y deja la captura física como gate
de release explícito.

**Corrección:** compilar/analisar todas las unidades desde `compile_commands.json`; activar selectivamente `bugprone`, `performance`, `cert` y reglas C++ Core Guidelines relevantes sin ahogar señales; harness POSIX con tasks reales y TSAN; ASan/UBSan; target stress con SystemView o ESP-IDF tracing. Agregar mutation/fault injection para ramas de error.

**Aceptación:** cobertura 25/25 TUs; cero race report no justificado; baseline de warnings versionado; cada suppress con owner y caducidad.

## P3 — Limpieza y deuda menor

### F-22. Código muerto, estados redundantes y ramas sin consumidor

**Estado de remediación (2026-09-06): ✅ corregido.** Se eliminaron de OTA la
constante `OTA_PROGRESS_INTERVAL_BYTES` y los miembros sin lectura
`runningPartition_` y `stagedSizeBytes_`. La política RF ahora representa el
contrato real: sólo puede publicar `BT` tras aplicar `ESP_COEX_PREFER_BT`, o
`UNKNOWN` antes de aplicarlo o si falla; se retiraron las ramas sin consumidor
`WIFI`/`BALANCE` y el snapshot ya no afirma `BALANCE` antes de inicializarse.
Los gates host impiden reintroducir esos símbolos. La declaración adelantada de
`commitLiveRuntimeConfig` se conservó porque no está muerta: sus llamadas
preceden a la definición en la misma TU.

**Vectores:** calidad  
**Confianza:** alta para los símbolos listados  

- `OTA_PROGRESS_INTERVAL_BYTES` está definido y no usado (`ShotStopperOta.cpp:47`).
- `runningPartition_` se asigna pero no se consulta (`ShotStopperOta.cpp:181`, `ShotStopperOta.h`).
- `stagedSizeBytes_` se asigna/resetea pero no participa en snapshot ni decisión (`ShotStopperOta.cpp:649/718`).
- Existe una declaración adelantada de `commitLiveRuntimeConfig` en el worker sin uso local.
- Las ramas de preferencia RF `WIFI`/`BALANCE` no parecen tener llamadores mientras el snapshot reporta una preferencia fija; eso puede ser código anticipatorio o telemetría engañosa.

No se identificaron rutas inalcanzables adicionales con certeza suficiente; compilador y analizadores están limpios. El código anterior debe eliminarse o conectarse a un requisito/test explícito.

## 5. Auditoría específica por vector solicitado

### 5.1 Concurrencia, FreeRTOS y multicore

- **Carreras demostrables:** F-01, F-02, F-03, F-04, F-07, F-09 y F-17.
- **Deadlocks:** no se encontró un ciclo ABBA actual. Sí existe lock anidado `advertMux_ -> mux_` y no hay grafo/orden formal; debe considerarse riesgo de regresión.
- **Inversión de prioridad:** los spinlocks no tienen herencia y deshabilitan interrupciones; F-12. Los mutexes task-only deben preferirse donde el tiempo no sea estrictamente escalar.
- **ISR:** la ruta de seguridad no invoca APIs FreeRTOS, por lo que no corresponde exigir `x...FromISR`. Usa `portENTER_CRITICAL_ISR` y acceso GPIO directo, patrones correctos. El defecto está en la publicación `volatile` del timer y en la cota no demostrada del lock compartido.
- **Barreras:** los builtins atómicos del contador de secuencia sí generan barreras, pero no vuelven atómico el payload ordinario. Una barrera no repara una data race C++.
- **Afinidad:** reparto global defendible, schedulability no demostrada; F-13.

### 5.2 Memoria, PSRAM e IRAM

- **PSRAM:** estrategia general correcta y verificada en mapa. Los buffers voluminosos se ubican externamente; asignación externa no degrada silenciosamente.
- **IRAM:** callback GPTimer aparece en IRAM y estado crítico permanece en DRAM. No se detectó abuso evidente de `IRAM_ATTR` para lógica no ISR.
- **Fugas/ciclo de vida:** fallos parciales en persistencia, relay timers y Network/Webhook; F-05, F-14, F-15, F-19.
- **Punteros colgantes:** riesgo concreto en `activeClient_`; F-07.
- **Fragmentación:** no hay containers/String crecientes en hot loops; riesgo de churn HTTP/TLS y restarts; F-18.
- **RAII:** chunk OTA interno sí usa una estrategia acotada; ownership de handles ESP sigue siendo manual.

### 5.3 Resiliencia y TWDT

- Tasks críticas se suscriben y alimentan el watchdog; watchdog de interrupciones, boot watchdog y rollback son controles positivos.
- Bloqueos relevantes: Serial/log dump, hash/relectura OTA, flash/NVS y llamadas HTTP/TLS; F-08, F-10, F-13.
- El timeout TWDT de 5 s con panic es correcto como red de último recurso, no como prueba de deadline.
- Ampliar globalmente a 30 s durante OTA reduce cobertura para todas las tareas; debe justificarse o rediseñarse.
- El manejo de códigos de error no es exhaustivo; F-14.

### 5.4 Calidad, arquitectura y mantenibilidad

- Pruebas funcionales e invariantes: fuertes.
- Límites de ownership: no suficientemente explícitos y ya violados.
- Complejidad/concentración: F-20.
- Código muerto confirmado: F-22.
- Herramientas: buen baseline, pero sin detector real de carreras y con cobertura incompleta; F-21.

## 6. Controles positivos que deben preservarse

1. **Fail-closed del relé:** rechazo de cierre si timers/watchdog/fault no están sanos.
2. **Temporizador de seguridad independiente:** ISR mínima, GPIO directo y sin APIs RTOS inapropiadas.
3. **Rollback OTA y validación:** staging, SHA y política de confirmación posterior al arranque.
4. **PSRAM explícita:** buffers pesados externos, reserva interna y stacks internos.
5. **Colas acotadas y generación BLE:** evitan crecimiento sin límite y descartan parte de los callbacks tardíos.
6. **Logging try-lock:** los productores críticos prefieren perder diagnóstico antes que bloquear control.
7. **Flash scratch con exclusión y timeout:** existe una intención clara de coordinar escrituras.
8. **Ausencia de asignación STL recurrente:** reduce fragmentación y jitter.
9. **Pruebas host extensas:** excelente base para añadir pruebas concurrentes/fault injection.

Las correcciones no deben reemplazar estas defensas por abstracciones que asignen memoria dinámicamente o bloqueen desde ISR.

## 7. Plan de corrección por fases

### Fase 0 — Contención y eliminación de UB crítica

**Objetivo:** recuperar un modelo de memoria válido y evitar estados operativos falsos.

1. Reemplazar flags `volatile` inter-task por atómicos lock-free o señales FreeRTOS apropiadas.
2. Corregir `IndependentSafetyTimer` con estado ISR-safe y validar memoria/IRAM.
3. Quitar del scale worker todo acceso directo a `runtimeConfig`, `session`, buzzer y `cupPresence`; introducir eventos/snapshot de configuración.
4. Reemplazar seqlocks inseguros, empezando por `ControlGate` y receta; eliminar fallback rasgado.
5. Corregir rollback de creación de `settings_persist` y exigir capability viva en dispatch.
6. Redefinir `BOOT_READY` con matriz de capacidades obligatorias.
7. Cerrar la vida útil NimBLE: cancelación, quiescencia y sincronización uniforme de handles.

**Gate de salida:** TSAN concurrente limpio en estos dominios; fault injection de inicialización; ninguna operación de seguridad consume un struct copiado concurrentemente; suite actual sigue verde.

### Fase 1 / P1 — Propietarios únicos y resiliencia de servicios ✅ COMPLETADA

**Objetivo:** eliminar carreras transversales y hacer transaccional cada servicio.

1. [x] F-08: logging dedicado, backpressure, drops y exportación lineal/chunked.
2. [x] F-09: transiciones OTA serializadas y publicación atómica.
3. [x] F-10: SHA incremental, journal espaciado y acceso flash/NVS coordinado.
4. [x] F-11: parseos JSON independientes, acotados y reentrantes.
5. [x] F-12: mutexes task-only, secciones ISR mínimas y DAG de locks.
6. [x] F-13: contratos temporales y telemetría de gaps/deadline misses.
7. [x] F-14: retornos críticos con política de fallo/rollback/diagnóstico.
8. [x] F-15: `Network::begin/stop` y Webhook reversibles con join.
9. [x] F-16: única instancia RTC en una unidad de traducción.
10. [x] F-17: métricas atómicas y snapshots versionados coherentes.

**Gate de salida de implementación:** ✅ superado mediante compilación IDF,
fault injection host, ASan/UBSan/TSAN y suite funcional. **Gate HIL de release:**
procedimiento definido, pendiente de ejecución sobre el dispositivo físico.

### Fase 2 — Determinismo temporal, memoria y desgaste

**Objetivo:** demostrar márgenes, no solo observar ausencia de watchdog.

**Estado de implementación (2026-09-06): 🟡 en curso.** El detalle resumible
y la distinción implementación/HIL se mantienen en `SCHEDULABILITY.md`,
`P2_RESOURCE_BUDGETS.md` y `P2_TARGET_TRACE.md`.

1. [~] Tabla de tareas con periodo/deadline/WCET/prioridad/bloqueo/stack/core:
   contrato e instrumentación implementados; WCET calificado HIL pendiente.
2. [~] Reducir spinlocks y medir tiempo máximo con interrupciones
   deshabilitadas: cinco grupos task-only migrados; traza target pendiente.
3. [~] Hacer event-driven el worker de balanza donde sea compatible con el
   protocolo: comandos/política/Companion/sonido ya notifican; callbacks del
   backend y calificación de latencia pendientes.
4. [x] Rediseñar checkpoints/hash OTA y presupuestar desgaste NVS: SHA
   incremental y máximo versionado de 6/7 escrituras por intento según slot.
5. [x] Reutilizar clientes HTTP o estabilizar su ciclo de vida: handle webhook
   persistente por URL, transporte reiniciable y cleanup posterior al join.
6. [~] Soak tests de heap por capability y de radio/red/flash combinados:
   capturador/analizador y matriz implementados; corridas 8 h/72 h pendientes.

**Gate de salida:** límites cuantitativos aprobados para jitter de control, deadline del corte independiente, edad de muestra, largest-free-block, stack margin y escrituras máximas por OTA.

### Fase 3 — Modularización y calificación continua

**Objetivo:** reducir la probabilidad de reintroducir las clases de fallo.

1. Separar SafetyKernel, Control, Scale, Network, Persistence y Diagnostics.
2. Sustituir `extern` globales por interfaces y mensajes de ownership explícito.
3. Incorporar RAII no asignante para handles.
4. Mantener 25/25 TUs con compile database; añadir clang-tidy, ASan/UBSan/TSAN.
5. Pruebas HIL de larga duración, brownout, desconexión RF, flash llena, host serie bloqueado y OTA interrumpida.
6. Versionar presupuesto temporal/memoria y fallar CI ante regresión.

**Gate de salida:** checklist de release reproducible, evidencia HIL, análisis de peor caso y revisión independiente del SafetyKernel.

## 8. Criterios de misión crítica propuestos

Antes de declarar aptitud, exigir como mínimo:

- Cero data races conocidas y TSAN limpio en modelos host concurrentes.
- Cero accesos inter-domain sin ownership, atómico o primitiva documentada.
- Ningún camino de ISR llama código no IRAM/DRAM-safe ni API task-only.
- Latencia máxima del corte independiente medida bajo peor carga y con margen documentado.
- Todos los recursos adquiridos tienen rollback probado por fault injection.
- Ningún ajuste se reconoce como aceptado sin garantía de persistencia o error explícito.
- Cada `esp_err_t` crítico se maneja; excepciones documentadas por código esperado.
- Heap interno, mayor bloque libre y stacks conservan margen después del soak definido.
- OTA tolera reinicio/brownout en cualquier frontera sin boot loop ni imagen ambigua.
- Estado `READY` tiene una definición contractual y verificable.
- Matriz de trazabilidad requisito → riesgo → control → prueba.

## 9. Riesgo residual y límites de esta auditoría

La revisión estática no puede demostrar tiempos máximos del bus flash, coexistencia RF, latencia del scheduler ni comportamiento eléctrico del relé. Tampoco valida integridad de alimentación, brownout real, EMI, rebote físico, aislamiento o el dispositivo de seguridad externo. Esos aspectos requieren instrumentación HIL y osciloscopio/logic analyzer.

No se afirmó un deadlock existente porque no se encontró un ciclo de locks; sí se reportaron las condiciones que hacen probable una regresión. Tampoco se reportó como defecto la ausencia de macros `FromISR` donde no se invocan APIs FreeRTOS. El objetivo es mantener el informe estricto: distinguir fallos demostrables, mitigaciones presentes y evidencia aún faltante.

## 10. Conclusión

La base es prometedora y muestra una intención seria de seguridad, especialmente en relé, OTA, PSRAM y pruebas. El salto pendiente a misión crítica consiste en volver **formal** lo que hoy es mayormente convencional: un único dueño por estado, publicación conforme al modelo C++, ciclos de vida con quiescencia, inicialización transaccional y presupuestos temporales medidos. Completar P0 cambia el firmware de “funcional con defensas” a “semánticamente seguro”; completar P1 y P2 aporta la evidencia necesaria para sostener esa seguridad bajo carga, fallos parciales y operación prolongada.
