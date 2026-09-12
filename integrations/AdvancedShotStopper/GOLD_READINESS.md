# Home Assistant Gold-readiness evidence

This custom integration is **Gold-ready**, not Gold-certified. Official tier
assignment requires later inclusion in Home Assistant Core and review there.

| Rule area | Status and evidence |
| --- | --- |
| UI configuration and uniqueness | Done: IP/host-only user flow, stable MAC unique ID, duplicate abort, takeover confirmation, and config-flow tests |
| Runtime data and async I/O | Done: typed `ConfigEntry.runtime_data`, injected Home Assistant session, `DataUpdateCoordinator`, and no blocking I/O |
| Test before configure/setup | Done: identity/API checks in the flow; registered callback, correlated test, and first refresh before platforms |
| Unload and availability | Done: platform unload and receiver unregister; five-minute coordinator reconciliation maps failures to Home Assistant availability/retry semantics |
| Reauthentication | Exempt: the intentionally open local API has no credentials to expire or renew |
| Reconfiguration | Done: current host/secret webhook ID, callback preview, same-device validation, candidate-first registration, correlated verification, rollback, and repair issue |
| Device and entities | Done: one device, stable unique IDs, entity-name semantics, translations, classes/units/icons, explicit enums, and non-optimistic preset select |
| Diagnostics | Done: host, callback URL/ID, and correlation data are excluded or redacted |
| Documentation | Done: installation, removal, entities, data updates, examples, limitations, migration, security, reconfigure, and troubleshooting |
| Tests and quality tools | CI runs pytest with module coverage ≥95%, Ruff, strict mypy, JSON validation, hassfest, and HACS validation |
| Discovery | Exempt: firmware advertises no supported discovery mechanism; mDNS is not reintroduced only to satisfy this rule |
| Discovery update info | Exempt with discovery: reconfigure safely changes the address while retaining the stable controller identity |
| Dynamic devices | Exempt: one config entry always represents exactly one fixed controller |
| Stale devices | Exempt: the integration never creates nested or dynamically discovered devices |
| Actions/triggers/conditions | Exempt: v1 exposes state and one native select; it registers no custom actions, triggers, or conditions |
| Entity categories/default disable | Exempt: every shipped entity is a primary user-facing shot or preset value; no noisy diagnostic entity is created |
| Repairs | Done for the only extra user-actionable state: failure of both candidate callback verification and remote rollback |

Before a Core submission, translate this evidence into Core's then-current
`quality_scale.yaml` and re-run the official checklist. Do not copy this custom
claim as an official tier.
