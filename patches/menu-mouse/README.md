# menu-mouse

UCMenu: the mouse on the Arena's collapsed panels.

The Arena shows one panel open at a time: GAMETYPE, RULES or ARENA (the maps), with MUTATORS beside RULES. The others
are collapsed to their selected row. A collapsed panel is inert to the mouse: the pointer over it gives it no focus and
the wheel there does nothing, so the keys and the pads go on acting on the open panel, and a click on any part of it
(its row, or the rule drawn on RULES' row) selects nothing there. A click on a collapsed panel opens it the way the keys
step to it, and keeps its selection:
- **a panel before the open one:** it opens as B goes back;
- **the panel right after the open one:** it opens as A on the open panel's list does (the game type accepted, then
  the rules);
- **a panel two steps on, or any collapsed panel while the mutators are up:** the error sound, nothing else;
- **while the panels move:** nothing.

Once a panel is open, its rows take the mouse as before. The keys and the pads are unchanged: only the engine's PC
mouse (`UnGUICore.cpp`) asks the panels whether they are inert to it.

- `ArenaListPanel`: `MouseInert` (true while the panel is not open), which the engine's mouse asks of the components
  under the pointer and their owners; `MouseInertClick`, the engine's call for a click on an inert panel; and
  `OnCollapsedClick`, which its page sets.
- `Arena_PanelMain`: `CollapsedPanelClicked`, bound to `pnl_Game`, `pnl_Rules` and `pnl_Maps`. It logs a `DevUI` line.

Depends on epic-fixes. A page that does not set `OnCollapsedClick` takes no click on its collapsed panels; the pages
whose panels are always open (the Armory, the map-list editor, Optimatch) answer `IsOpen` true and are not inert.
