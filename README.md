# MQCharInfo

C++ plugin that publishes character data via the Actors system and exposes a Lua module with the same API and data shape as the reference `examples/charinfo.lua`.

## Lua module

Load the module with:

```lua
local Charinfo = require("plugin.charinfo")
```

**Requirement:** `require("plugin.charinfo")` requires MQ2Lua to support the `plugin.*` package loader (e.g. resolving `plugin.charinfo` via `GetPluginProc("MQ2CharInfo", "CreateLuaModule")`). If that loader is not present, it must be added in MQ2Lua (outside this plugin).

## API (read-only)

- `GetInfo(name)` – table for peer `name`, or `nil`
- `GetPeers()` – sorted array of peer names
- `GetPeerCnt()` – number of peers
- `GetPeer(name)` – proxy table with peer data plus `Stacks(spell)` and `StacksPet(spell)`; `Charinfo(name)` is equivalent to `GetPeer(name)`
