# MQCharInfo

C++ plugin that publishes character data via the Actors system and exposes a Lua module with the same API and data shape as the reference `examples/charinfo.lua`.

## Lua module

MQ2Lua’s package loader supports `plugin.*` and `plugin/*`. Load the module with:

```lua
local charinfo = require("plugin.charinfo")
```

The plugin is built as **MQcharinfo.dll** so its canonical name is **`charinfo`**. Use **`plugin.charinfo`** (lowercase) so the loader finds the module even when the require name is lowercased.

The loader resolves the plugin by canonical name (from the DLL: **MQcharinfo** → **charinfo**) and calls `GetPluginProc(plugin, "CreateLuaModule")`. No `.def` is required. If you see *"does not export CreateLuaModule"*, the plugin may be unloaded or the export may be missing.

## API (read-only)

- `GetInfo(name)` – table for peer `name`, or `nil`
- `GetPeers()` – sorted array of peer names
- `GetPeerCnt()` – number of peers
- `GetPeer(name)` – proxy table with peer data plus `Stacks(spell)` and `StacksPet(spell)`;
