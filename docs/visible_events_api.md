# Visible action journal

`GameSession` owns one private event stream for every player. Python obtains
only the stream of the current player:

```python
cursor = 0
packet = env.visible_events_numpy(cursor)
cursor = int(packet["next_cursor"])
```

The journal is intended for modelling dynamic state outside fog of war: units
that moved into fog, combat results, unit creation/removal, upgrades and tile
mutations. It is not a second copy of the visible map.

## Visibility and privacy rules

An event is appended to an observer's stream only when the observer performed
the action, or its source or target tile was visible immediately before or
after the action.

- `source_index` is `-1` when the source tile was hidden.
- `target_index` is `-1` when the target tile was hidden.
- Since turns are sequential and public, an observed action always identifies
  `actor_player` and `actor_tribe`; this does not reveal its hidden unit.
- A source/target unit's type, private observed id and HP are present only
  when that unit is visible to the observer (or belongs to that observer).
- Death of a target is exposed only if the target was visible. Death of an
  attacker due to retaliation is exposed only if the attacker was visible.
- Splash victims are included only if their own tiles were visible.

`observed_unit_id` is private to an observer. It is a stable id assigned when
that observer sees a unit; it is **not** the engine's `UnitId` and therefore
does not reveal hidden spawn order.

## VectorGameEnv window

`VectorGameEnv(visible_event_history=K)` provides the same filtered data as a
fixed GPU-friendly suffix in every batch. It returns dense
`visible_event_features`, `visible_event_sequence`,
`visible_event_action_sequence`, `visible_event_mask`,
`visible_event_affected`, and `visible_event_affected_mask` arrays. Records
are chronological and right-aligned; the newest valid event is at `K - 1`.
See [VectorGameEnv](vector_env.md#visible-event-window) for tensor shapes and
feature layouts. The default `K=0` leaves this encoding disabled, and vector
environments retain only their configured suffix rather than an unbounded
cursor history.

## Event identity

Each event has these NumPy columns of length `E`:

| Field | dtype | Meaning |
| --- | --- | --- |
| `sequence` | `uint64` | Cursor private to this player's stream. |
| `action_sequence` | `uint64` | Shared id of one world action across observer streams. |
| `round` | `int32` | Full round before the action. |
| `turn` | `int32` | Engine turn after the action. |
| `type_id` | `int16` | Event category. |
| `flags` | `uint16` | Source/target visibility flags. |
| `actor_player`, `actor_tribe` | `int16` | Public active player and tribe. |
| `source_index`, `target_index` | `int32` | Map index, or `-1` when unknown. |

The GUI renders indexes as `(x, y)`. Convert in Python with `x = index %
width`, `y = index // width`.

## Event payload

All following fields are NumPy columns of length `E`; `-1` means unavailable
or not applicable.

| Fields | Meaning |
| --- | --- |
| `source_observed_unit_id`, `source_unit_type`, `source_unit_hp_before`, `source_unit_hp_after` | Observation-filtered source-unit state. |
| `target_observed_unit_id`, `target_unit_type`, `target_unit_hp_before`, `target_unit_hp_after` | Observation-filtered target-unit state. |
| `damage`, `hp_before`, `hp_after` | Primary combat target result. |
| `unit_destroyed`, `source_unit_destroyed` | Primary target / retaliating attacker death flags. |
| `spawn_type` | Type created by `SpawnUnit`. |
| `tile_action_kind` | Concrete tile action such as Hunt, Fishing or BuildRoad. |
| `building_type` | Constructed building type. |
| `unit_upgrade_kind`, `upgraded_unit_type` | Upgrade choice and unit type after upgrade. |

The emitted categories are move, attack, heal, spawn, build, concrete tile
actions, unit removal and unit upgrade. End turn, technology, stars/income,
tile revelation and city capture/founding are not journal events. The latter
state belongs to the player's observed-map memory.

## Splash victims: NumPy CSR layout

An attack can affect multiple units. The packet stores that variable-length
data without Python objects:

```python
start = packet["affected_offsets"][event_index]
end = packet["affected_offsets"][event_index + 1]

victim_ids = packet["affected_observed_unit_id"][start:end]
victim_tiles = packet["affected_tile_index"][start:end]
```

The flat columns are `affected_observed_unit_id`, `affected_tile_index`,
`affected_unit_type`, `affected_damage`, `affected_hp_before`,
`affected_hp_after`, `affected_destroyed` and `affected_splash`.

## `map_tokens` are not a `Game` snapshot

`map_tokens` and the visible-action journal are safe model inputs. They do not
contain enough data to reconstruct a legal engine `Game`: hidden unit ids,
turn-internal flags, technology, economy, pending rewards and other mechanics
are intentionally absent.

For inference or MCTS under fog, build an independent `ObservedWorld` from
the visible map tokens, persistent observed-map memory and this journal. Do
not patch tokens into an existing authoritative `Game`, because that can
accidentally retain hidden state from the original game.
