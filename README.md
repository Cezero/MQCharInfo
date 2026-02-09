# MQCharinfo

Publishes your character’s state over the Actors (post office) system so other clients can see it, and exposes a Lua module to read peer data. The data shape and API are similar to NetBots-style peer info: vitals, buffs, zone, target, experience, and more.

## Loading the Lua module

Load the module with:

```lua
local charinfo = require("plugin.charinfo")
```

The plugin DLL is **MQCharinfo.dll** (canonical name **Charinfo**). Use **`plugin.charinfo`** in Lua; the loader resolves it via case-insensitive lookup. If you see *"does not export CreateLuaModule"*, the plugin may be unloaded.

You can instead use **`require("mqcharinfo")`** for the same API with EmmyLua annotations (IDE completion and hover docs). Copy the **mqcharinfo** folder from the plugin directory into your MacroQuest install's **lua/** folder (e.g. `MacroQuest/lua/mqcharinfo/`) so `require("mqcharinfo")` is found.

**Configuration:** For consistent behavior across clients (e.g. postoffice mailbox), list the plugin in your `[Plugins]` section as **MQCharinfo** or **charinfo**, not **MQCharInfo**. This ensures all clients get the same plugin identity and can see each other's data.

---

## API (read-only)

| Function | Description |
|----------|-------------|
| `charinfo.GetInfo(name)` | Returns the peer table for character `name`, or `nil` if not found. |
| `charinfo.GetPeers()` | Returns a sorted array of peer character names. |
| `charinfo.GetPeerCnt()` | Returns the number of peers. |
| `charinfo(name)` | Same as `GetInfo(name)` (module is callable). |

**Stacks / StacksPet** (on the table returned by `GetInfo(name)` and `charinfo(name)`):

These are methods and must be called with a colon: use `peer:Stacks(spell)` and `peer:StacksPet(spell)`, not `peer.Stacks(spell)`.

- `peer:Stacks(spell)` — `true` if the given spell (name or ID string) would stack with all of this peer’s long and short buffs. Accepts a string or number (spell ID).
- `peer:StacksPet(spell)` — same, but for the peer’s pet buffs.

---

## Peer data structure

Each peer is a table with the following keys. All data is read-only and comes from the last publish received from that character.

### Top-level fields

| Key | Type | Description |
|-----|------|-------------|
| `Name` | string | Character name. |
| `ID` | number | Spawn ID. |
| `Level` | number | Level. |
| `PctHPs` | number | HP percent (0–100). |
| `PctMana` | number | Mana percent (0–100). |
| `TargetHP` | number | Current target’s HP percent (0–100). |
| `FreeBuffSlots` | number | Free long buff slots. |
| `Detrimentals` | number | Count of detrimental buffs. |
| `CountPoison` | number | Poison counters. |
| `CountDisease` | number | Disease counters. |
| `CountCurse` | number | Curse counters. |
| `CountCorruption` | number | Corruption counters. |
| `PetHP` | number | Pet HP percent (0–100). |
| `MaxEndurance` | number | Max endurance. |
| `CurrentHP` | number | Current HP. |
| `MaxHP` | number | Max HP. |
| `CurrentMana` | number | Current mana. |
| `MaxMana` | number | Max mana. |
| `CurrentEndurance` | number | Current endurance. |
| `PctEndurance` | number | Endurance percent (0–100). |
| `PetID` | number | Pet spawn ID (0 if none). |
| `PetAffinity` | boolean | Whether the character has the pet AA (e.g. Companion’s Suspension). |
| `NoCure` | number | No-cure detrimental counter. |
| `LifeDrain` | number | Life drain counter. |
| `ManaDrain` | number | Mana drain counter. |
| `EnduDrain` | number | Endurance drain counter. |
| `Version` | number | Protocol version (e.g. 1.1). |
| `CombatState` | number | Combat state (e.g. ACTIVE, COMBAT, RESTING). |
| `CastingSpellID` | number | Spell ID currently being cast (0 if not casting). |

### Arrays (1-based in Lua)

| Key | Type | Description |
|-----|------|-------------|
| `State` | array of strings | Active state flags, e.g. `"STAND"`, `"LEVITATING"`, `"SIT"`, `"MOUNT"`. |
| `BuffState` | array of strings | Active buff-state flags (e.g. `"Slowed"`, `"Hasted"`, `"Cursed"`). |
| `Buff` | array of tables | Long buffs. Each entry: `Duration`, `Spell` (table with `Name`, `ID`, `Category`, `Level`). |
| `ShortBuff` | array of tables | Short buffs. Same structure as `Buff`. |
| `PetBuff` | array of tables | Pet buffs. Same structure as `Buff`. |
| `Gems` | array of tables | Spell gems in order. Each entry: `ID`, `Name`, `Category`, `Level` (full spell info). Use `Gems[1].ID`, `Gems[1].Name`, etc. |
| `FreeInventory` | array of numbers | Free inventory counts by size (indices 1–5 correspond to sizes 0–4). |

### Subtables

**Class**

| Key | Type |
|-----|------|
| `Name` | string |
| `ShortName` | string |
| `ID` | number |

**Target**

| Key | Type |
|-----|------|
| `Name` | string |
| `ID` | number |

**Zone**

| Key | Type | Description |
|-----|------|-------------|
| `Name` | string | Zone long name. |
| `ShortName` | string | Zone short name. |
| `ID` | number | Zone ID. |
| `InstanceID` | number | Instance ID. |
| `X`, `Y`, `Z` | number | Position in zone. |
| `Heading` | number | Heading (degrees). |
| `Distance` | number or nil | **Client-side only.** 3D distance from your character to this peer. `nil` if the peer is not in the same zone/instance as you. |

**Experience** (present when data is available)

| Key | Type |
|-----|------|
| `PctExp` | number |
| `PctAAExp` | number |
| `PctGroupLeaderExp` | number |
| `TotalAA` | number |
| `AASpent` | number |
| `AAUnused` | number |
| `AAAssigned` | number |

**MakeCamp** (present when MQ2MoveUtils is loaded and camp data is sent)

| Key | Type |
|-----|------|
| `Status` | number |
| `X`, `Y` | number |
| `Radius` | number |
| `Distance` | number |

**Macro** (present when data is available)

| Key | Type |
|-----|------|
| `MacroState` | number (0 = none, 1 = running, 2 = paused) |
| `MacroName` | string |

---

## Example

```lua
local charinfo = require("plugin.charinfo")

local names = charinfo.GetPeers()
for i = 1, #names do
    local peer = charinfo.GetInfo(names[i])
    if peer then
        print(peer.Name, "HP:", peer.PctHPs, "Zone:", peer.Zone.ShortName)
        if peer.Zone.Distance then
            print("  Distance:", peer.Zone.Distance)
        end
        if peer.Stacks and peer:Stacks("Clarity II") then
            print("  Clarity II would stack")
        end
    end
end
```

---

## Settings panel

The plugin adds a **plugins/Charinfo** settings panel that lists all peers and their data in a tree (same structure as above). Use it to inspect what each peer is publishing.
