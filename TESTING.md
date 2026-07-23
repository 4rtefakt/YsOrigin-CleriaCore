# How to test (ADHD-friendly)

Quick checklists. One action per line. Each says what you should see.
Launch = the **CleriaCore** Start-menu shortcut. Skip any section you don't feel like.

---

## ⭐ Test the cutscene fixes (needs events ON)

Events are OFF by default right now. To turn them ON for a test, open **PowerShell**
(the blue terminal) and paste this ONE line:

```powershell
$env:CLERIA_EVENTS=1; & "C:\Users\kores\repos\YsOrigin-CleriaCore\build\Release\cleria-view.exe" "C:\Users\kores\repos\YsOrigin-CleriaCore\assets"
```

⚠️ It stays ON for that terminal window. To go back to normal: close the terminal,
or run `$env:CLERIA_EVENTS=0`. (Or just double-click the shortcut — that's always events-OFF.)

**With events ON, check these 3 things:**

1. **NPCs show up in cutscenes**
   - New Game → Yunica → into the tower (S_1000).
   - A cutscene plays. ➡️ You should SEE the NPC character(s), not just a floating text box.
   - ❌ Before: text talked but nobody was there.

2. **Camera is NOT in the floor**
   - Same intro cutscene.
   - ➡️ The camera should look at the room from a normal angle, not be buried at floor level.
   - ❌ Before: view was stuck at ground level staring sideways.

3. **Barrier needs a real kill (S_1001)**
   - Walk to the next room (a barrier fight room).
   - ➡️ An enemy should be there, and the barrier should stay UP until you kill it.
   - ❌ Before: the barrier "cleared" the instant you walked in, no enemy.

**Also handy:** press **ESC** (or **Backspace**) during any cutscene to skip it.
➡️ It should jump to the end and you can move normally after.

---

## Save / Load (works now, events OFF is fine)

1. Play a bit, walk to a **goddess statue**, press **E**.
2. Pick **Save** → pick a slot → **Enter**. ➡️ "Saved to slot N".
3. Quit. Relaunch. Title → **Load Game** → your slot → **Enter**.
4. ➡️ You're back where you saved, same level/HP/character.

Blessings (**E** at statue → "Accept divine blessing") just say "not implemented" for now — that's expected.

---

## If something looks wrong

Tell me: **which room**, **what you did**, **what you saw** (a screenshot is perfect).
That's all I need — I can read the game's code to find the rest.

---

## For me (not you): the controls cheat-sheet
WASD move · LMB attack · Space jump · **E** interact/statue · **Esc** pause **or** skip cutscene ·
**Backspace** skip cutscene · Tab swap character · 1-9 take exit N · T debug-talk · F free-fly cam
