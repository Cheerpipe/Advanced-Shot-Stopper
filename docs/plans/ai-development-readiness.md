# Plan transversal para desarrollo AI-first y seguro

Estado: `IMPLEMENTED — HIL PENDING`  
Idioma canónico: inglés técnico para la documentación nueva  
Alcance: dos fases; primero DX/IA y automatización, después modularización incremental

## Objetivo y principios

Hacer el repositorio más fácil de explorar, modificar, validar y reanudar con asistentes de IA, reduciendo consumo de tokens, contexto, memoria de trabajo, tiempo de compilación y créditos de CI.

La seguridad y la calidad tienen prioridad absoluta. Ninguna optimización puede omitir validación, evidencia HIL, revisión de concurrencia, límites de memoria o comprobaciones del relé. Los cambios que toquen relé, máquina, ISR, watchdog, boot, GPIO, particiones, OTA de seguridad o activación remota se tratan como críticos.

## Diagnóstico de partida

- La documentación técnica es amplia y valiosa, pero carece de un índice de navegación por subsistema, fuente, test y comando.
- Hay aproximadamente 77.000 líneas entre firmware, tests y scripts; algunos archivos superan 5.000–12.000 líneas.
- Existen 48 scripts y alias, pero no una entrada canónica ni una política uniforme de salida.
- El runner principal recompila numerosas variantes/sanitizers y puede instalar dependencias implícitamente.
- `.cursorignore` está vacío aunque existen builds, componentes gestionados, reportes y dependencias locales de gran tamaño.
- No hay CI versionado con gates proporcionales al riesgo.
- El handoff está descrito en `AGENTS.md`, pero no existe un formato compacto y operativo de reanudación.

## Fase 1 — Organización, contexto y tooling

### 1. Línea base y estado de trabajo

- [x] Materializar este plan como checklist local y mantenerlo actualizado por unidad completada.
- [x] Registrar tiempos, volumen de salida y artefactos de los flujos actuales: orientación, host tests, Web UI, build IDF y análisis.
- [x] Congelar invariantes de seguridad: relé fail-open, arranque seguro, límites temporales, watchdog, ausencia de activación remota por defecto y ownership de recursos.
- [x] Usar un único `docs/handoff/SESSION_HANDOFF.md` local, ignorado por Git, para sesiones interrumpidas.

### 2. Navegación documental progresiva

- [x] Crear `docs/README.md` como índice canónico: necesidad → documento → fuentes → tests → comando.
- [x] Crear un `PROJECT_CHARTER` breve con propósito, hardware soportado, no-objetivos y límites de seguridad.
- [x] Crear `VALIDATION.md` con la matriz de gates y enlaces a BUILD, SCRIPTS, STATIC_ANALYSIS, MANUAL_TEST_PLAN y evidencia HIL.
- [x] Crear `CONTRIBUTING.md` con el flujo descubrir → clasificar riesgo → cambiar → validar → documentar → entregar.
- [x] Crear `docs/AI_WORKFLOW.md` con búsqueda progresiva, límites de contexto, handoff y reanudación.
- [x] Mantener inglés técnico como idioma canónico y evitar copias bilingües completas.
- [x] Crear `docs/decisions/` para ADRs breves sobre decisiones duraderas de arquitectura, seguridad o interfaces.
- [x] Mover auditorías e informes históricos a `docs/audits/`, marcados como históricos, preservando enlaces.
- [x] Añadir comprobación de enlaces, rutas y referencias del índice.

### 3. Instrucciones para agentes

- [x] Reducir `AGENTS.md` raíz a reglas globales: prioridades, búsqueda, permisos, riesgo, validación, Git y sesión.
- [x] Añadir instrucciones progresivas para firmware, Web UI, scripts y biblioteca BLE, sin duplicar reglas globales.
- [x] Exigir que el agente cargue inicialmente solo `AGENTS.md`, `docs/README.md` y la instrucción del subsistema afectado.
- [x] Documentar fuentes editables, archivos generados, dependencias vendorizadas y directorios que no deben cargarse masivamente.
- [x] Prohibir flashear, usar OTA, controlar el relé o ejecutar HIL salvo petición explícita del usuario.
- [x] Definir handoff máximo de 8 KiB/120 líneas con: objetivo, riesgo, SHA base, estado del worktree, decisiones, invariantes, tareas, siguiente acción, archivos, comandos, resultados, artefactos y bloqueos.
- [x] Al reanudar, verificar SHA/diff y reutilizar resultados registrados si las entradas no cambiaron.
- [x] Eliminar el handoff al completar; las decisiones permanentes pasan a documentación o ADR.
- [x] Añadir solo un adaptador mínimo para Cursor que apunte al mismo conjunto de instrucciones.

### 4. Fachada de scripts y control de verbosidad

- [x] Añadir `./scripts/dev` como fachada estable, conservando los alias existentes durante la transición.
- [x] Exponer `doctor`, `context`, `test`, `validate`, `build`, `analyze`, `flash`, `monitor`, `ota` y `clean`.
- [x] Incorporar `--verbosity compact|normal|verbose` y `SHOTSTOPPER_VERBOSITY`.
- [x] Usar `normal` en TTY y `compact` en CI/no-TTY; limitar éxito compacto a 20 líneas y fallo compacto a 80 líneas.
- [x] Guardar logs completos y resumen JSON en `artifacts/runs/<run-id>/`, con un resumen latest sin secretos.
- [x] Incluir en el resumen esquema, comando, riesgo, estado, SHA, entradas no secretas, duración, checks, logs, artefactos y validaciones manuales pendientes.
- [x] Eliminar instalaciones implícitas; `npm ci` y otros bootstrap de red deben ser comandos explícitos.
- [x] Mantener credenciales fuera de argv, logs, JSON y handoff.
- [x] Hacer que flash/OTA no interactivos requieran confirmación explícita y nunca omitan verificaciones de imagen por defecto.
- [x] Separar claramente la verbosidad de scripts del logging runtime del firmware.

### 5. Validación incremental y clasificación de riesgo

| Nivel | Cambios | Gate mínimo |
|---|---|---|
| R0 | Documentación/meta no crítica | Contrato, enlaces y formato |
| R1 | Web UI, tests, tooling y lógica pura | R0 + tests enfocados + assets |
| R2 | BLE, red, persistencia, OTA y build | Host completo + ASan/UBSan + TSAN + arquitectura + builds |
| R3 | Relé, máquina, ISR, watchdog, boot, GPIO, particiones, control remoto o desconocido | R2 + warnings/cppcheck + variantes + HIL/manual |
| Release | Imagen candidata | Análisis completo, recursos, HIL, manual y soak aplicable |

- [x] Implementar clasificación por paths; lo desconocido escala a R3.
- [x] Permitir overrides solo para aumentar el riesgo.
- [x] Migrar host tests a CMake/CTest incremental con presets normal, ASan/UBSan y TSAN.
- [x] Dividir el host test monolítico y `check_web_assets.js` por dominio, compartiendo fixtures.
- [x] Eliminar skips silenciosos: dependencias ausentes deben producir estados explícitos y fallar los perfiles que las requieran.
- [x] Reutilizar targets y compilation databases dentro de un mismo gate.
- [x] Comparar tamaño de imagen y regiones de memoria contra la línea base.

### 6. CI por riesgo

- [x] Añadir workflow con jobs `classify`, `fast`, `host`, `idf`, `analysis` y agregador `gate`.
- [x] Ejecutar `fast` en todos los PR y activar jobs adicionales según R0–R3.
- [x] Construir n8r4 y n16r8 para R2/R3; reutilizar resultados en análisis posteriores.
- [x] Reservar clang-tidy, IWYU y GCC analyzer completos para nightly/manual/release; mantener cppcheck y warnings en R3.
- [x] Activar cancelación de ejecuciones obsoletas, cachés por lockfile/toolchain y retención corta de artefactos.
- [x] Usar permisos mínimos, acciones fijadas y ningún secreto en PRs.
- [x] No ejecutar flash, OTA ni HIL desde CI hospedado.
- [x] Añadir plantilla de PR con riesgo, invariantes, validación, HIL requerido y documentación.

### 7. Reducir contexto inútil

- [x] Poblar `.cursorignore` con `.git`, builds, `idf/managed_components`, `node_modules`, reports generados, artefactos y assets binarios.
- [x] Mantener accesibles fuentes, lockfiles, configuración IDF, documentación de seguridad y tests.
- [x] Hacer que `scripts/dev context <area>` muestre primero rutas y descripciones, no contenido masivo.
- [x] Añadir tests de rutas para las áreas safety, control, machine, scale, BLE, network, OTA, persistence, Web, build y tests.
- [x] Añadir `doctor` y `clean --dry-run`; ninguna limpieza amplia será automática.

## Fase 2 — Modularización segura

- [x] Crear subdirectorios alineados con la arquitectura existente: safety, control, scale, network, persistence, diagnostics y platform.
- [x] Mantener fachadas estables mientras se extrae un servicio por PR.
- [x] Separar primero fixtures/tests y diagnósticos; después red/persistencia; dejar control físico y safety para PRs R3 aislados.
- [x] No mezclar movimientos con funcionalidades, migraciones NVS, cambios GPIO ni cambios de comportamiento del relé.
- [x] Reducir los legacy roots a menos de 2.000 líneas y limitar nuevos archivos de implementación a aproximadamente 1.500 líneas.
- [x] Ratchetear los límites de arquitectura hacia abajo; nunca elevarlos para admitir nueva funcionalidad.
- [x] Mantener invariantes y contratos HTTP, BLE, OTA, serial, persistencia y Web UI.
- [~] Validar cada extracción con el gate correspondiente y evidencia HIL cuando alcance rutas de actuación.
  - Gate R3 automatizado completo; HIL físico pendiente porque no se solicitó acceso a hardware.

## Interfaces nuevas y compatibilidad

- `scripts/dev` será la entrada nueva; los scripts actuales seguirán funcionando durante la transición.
- Los códigos normalizados serán `0` éxito, `1` fallo de validación, `2` uso/configuración inválida y `127` dependencia ausente.
- El resumen JSON tendrá `schemaVersion` y nunca incluirá contraseñas, tokens o credenciales.
- No se cambiarán APIs de firmware, defaults GPIO, esquemas NVS, endpoints, protocolo BLE ni semántica del relé como parte de este plan.
- La evidencia HIL será referenciada, nunca simulada por un gate automatizado.

## Pruebas y aceptación

- [x] Un agente nuevo localiza implementación, docs, tests y comando leyendo solo `AGENTS.md`, `docs/README.md` y una instrucción scoped.
- [x] La orientación inicial no supera aproximadamente 16 KiB.
- [x] Ningún test instala dependencias, accede a hardware o usa red implícitamente.
- [x] Un handoff interrumpido permite reanudar sin repetir exploración ni checks válidos.
- [x] El clasificador tiene casos golden R0–R3 y desconocido→R3.
- [x] R0 no instala ESP-IDF; R2/R3 construye ambos targets soportados en CI.
- [x] La refactorización conserva todos los escenarios, sanitizers, variantes y checks actuales.
- [x] No hay regresiones de tamaño o memoria fuera de presupuestos aceptados.
- [x] Todo R3 identifica pruebas HIL/manuales pendientes y no se declara release-ready sin evidencia.
- [x] Se realiza un simulacro de orientación, interrupción, handoff y reanudación y se compara con la línea base.

## Supuestos adoptados

- Calidad y seguridad prevalecen sobre tokens, velocidad y créditos.
- El núcleo de instrucciones será agnóstico a herramientas, con adaptadores mínimos.
- Planes y handoffs serán locales/efímeros; solo decisiones y evidencia curada se versionarán.
- La documentación nueva usará inglés técnico canónico.
- La primera fase no hará una reorganización masiva del firmware.
- Se mantendrán intactos los archivos locales ignorados existentes durante la implementación.
