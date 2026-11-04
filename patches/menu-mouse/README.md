# menu-mouse

UCMenu: the mouse on the Arena's collapsed panels.

The Arena shows one panel open at a time: GAMETYPE, RULES or ARENA (the maps), with MUTATORS beside RULES. The others
are collapsed to their selected row. A mouse click on a collapsed panel opens it the way the keys step to it, and keeps
its selection:
- **a panel before the open one:** it opens as B goes back;
- **the panel right after the open one:** it opens as A on the open panel's list does (the game type accepted, then
  the rules);
- **a panel two steps on:** nothing (an error sound), as for any collapsed click while the mutators are up or while
  the panels move.

The row under the pointer is not selected. A collapsed panel draws its selected row in its first slot, where a click
would select another row. Once a panel is open, its rows take clicks as before. The keys and the pads are unchanged:
only the mouse's press asks a list for the row under the pointer (the engine's GUI, `UnGUICore.cpp`).

- `AvarisVertList`: `OnMouseRow`, which `InternalOnClick` asks before it selects the row under the pointer.
- `ArenaListPanel`: `MouseRowClicked`, the click a collapsed panel takes, and `OnCollapsedClick`, which its page sets.
- `Arena_PanelMain`: `CollapsedPanelClicked`, bound to `pnl_Game`, `pnl_Rules` and `pnl_Maps`. It logs a `DevUI` line.

Depends on epic-fixes. A page that does not set `OnCollapsedClick` takes no click on its collapsed panels.
