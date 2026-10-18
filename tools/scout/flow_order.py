#!/usr/bin/env python3
"""Order in which the launcher connects and initialises its systems (finding 37, question 2).

    .venv/bin/python tools/scout/flow_order.py

Opens no game file.  It replays, on the names, the algorithm read from launcher.dll:
  launcher+0x94e0   the table of (DLL, interface) pairs the inner group creates, in order
  launcher+0x14940  "LOADING DEPENDENCIES": slot 6 of every system, missing ones are added
  launcher+0x12d80  closes the dependency sets (if A needs B, A needs what B needs)
  launcher+0x15dc0  pass 1: for i in 1..n-1, for j in 0..i-1: if order[j] needs order[i], swap;
                    repeated until nothing changes.  pass 2: for i in 1..n-1: if order[i] <
                    order[i-1] and order[i] does not need order[i-1], swap; repeated likewise.
The dependency tables are the three that tools/scout/flow_deps.py finds in the boot-path DLLs.
The result is the order of the Connect (slot 1) and Init (slot 4) calls of the inner group.
"""

CREATE = [  # launcher+0x94e0, in the order the code stores them, then the two calls after the table
    ("engine.dll", "VCvarQuery001"),
    ("inputsystem.dll", "InputSystemVersion001"),
    ("vphysics.dll", "VPhysics031"),
    ("materialsystem.dll", "VMaterialSystem083"),
    ("datacache.dll", "MDLCache004"),
    ("studiorender.dll", "VStudioRender026"),
    ("engine.dll", "VAvi001"),
    ("engine.dll", "VBik001"),
    ("vguimatsurface.dll", "VGUI_Surface031"),
    ("vgui2.dll", "VGUI_ivgui008"),
    ("engine.dll", "VENGINE_LAUNCHER_API_VERSION004"),
    ("engine.dll", "VDataCache003"),
    ("engine.dll", "VPrecacheSystem001"),
    ("vstdlib.dll", "VProcessUtils002"),
]
DEPS = {  # tools/scout/flow_deps.py
    "VGUI_Surface031": [("localize.dll", "Localize_001"), ("inputsystem.dll", "InputStackSystemVersion001"),
                        ("materialsystem.dll", "VMaterialSystem083")],
    "VGUI_ivgui008": [("localize.dll", "Localize_001"), ("vgui2.dll", "VGUI_Surface031")],
    "InputStackSystemVersion001": [("inputsystem.dll", "InputSystemVersion001")],
}


def main():
    systems = [n for _, n in CREATE]
    lib = {n: d for d, n in CREATE}
    loaded = []
    for d, _ in CREATE:
        if d not in loaded:
            loaded.append(d)
    need = {}
    i = 0
    while i < len(systems):                      # 0x14940: the count is re-read every turn
        s = systems[i]
        need[s] = []
        for d, n in DEPS.get(s, []):
            need[s].append(n)
            if n not in systems:
                if d not in loaded:
                    loaded.append(d)
                systems.append(n)
                lib[n] = d
        i += 1
    changed = True                               # 0x12d80
    while changed:
        changed = False
        for s in systems:
            for n in list(need[s]):
                for k in need.get(n, []):
                    if k not in need[s] and k != s:
                        need[s].append(k)
                        changed = True
    order = list(range(len(systems)))            # 0x15dc0, pass 1
    changed = True
    while changed:
        changed = False
        for i in range(1, len(order)):
            for j in range(i):
                if systems[order[i]] in need[systems[order[j]]]:
                    order[i], order[j] = order[j], order[i]
                    changed = True
    done = False                                 # pass 2
    while not done:
        done = True
        for i in range(1, len(order)):
            left, right = order[i - 1], order[i]
            if right > left or systems[left] in need[systems[right]]:
                continue
            order[i - 1], order[i] = right, left
            done = False
    print("DLLs in the order the inner group first asks for them: " + ", ".join(loaded))
    print("Connect and Init order of the inner group:")
    for k, o in enumerate(order):
        s = systems[o]
        print("  %2d  %-34s %-20s%s" % (k + 1, s, lib[s], ("  needs " + ", ".join(need[s])) if need[s] else ""))


if __name__ == "__main__":
    main()
