#!/usr/bin/env python3
"""A small Microsoft C++ (x64) demangler for the export names of tier0.dll and vstdlib.dll.

Not a complete demangler: it covers what the two export tables use (member functions with the
access/virtual/static letter, constructors, destructors, operators, vftables, data, templates with
type and integer arguments, pointers, references, function pointers, enums, classes, structs,
builtin types, back-references to names and to function parameters).  Anything it does not
understand makes `demangle()` return ok=False with a partial text, and the argument that failed is
reported as "unknown"; it never guesses.

The grammar follows the publicly documented shape of the scheme (and the behaviour of LLVM's
Microsoft demangler, which the back-reference rules were checked against): a name table of up to ten
identifiers and a table of up to ten function-parameter spans that are more than one character long.

    python tools/scout/gate2_demangle.py --selftest
    python tools/scout/gate2_demangle.py '?Start@CThread@@UEAA_N_KW4ThreadPriorityEnum_t@1@@Z'
"""

import sys

BUILTIN = {
    "X": "void", "D": "char", "C": "signed char", "E": "unsigned char", "F": "short", "G": "unsigned short",
    "H": "int", "I": "unsigned int", "J": "long", "K": "unsigned long", "M": "float", "N": "double",
    "O": "long double",
}
BUILTIN_US = {
    "N": "bool", "J": "__int64", "K": "unsigned __int64", "W": "wchar_t", "S": "char16_t", "U": "char32_t",
    "D": "__int8", "E": "unsigned __int8", "F": "__int16", "G": "unsigned __int16", "H": "__int32",
    "I": "unsigned __int32", "L": "__int128", "M": "unsigned __int128",
}
OPERATORS = {
    "0": "ctor", "1": "dtor", "2": "operator new", "3": "operator delete", "4": "operator=", "5": "operator>>",
    "6": "operator<<", "7": "operator!", "8": "operator==", "9": "operator!=", "A": "operator[]",
    "B": "operator <cast>", "C": "operator->", "D": "operator*", "E": "operator++", "F": "operator--",
    "G": "operator-", "H": "operator+", "I": "operator&", "J": "operator->*", "K": "operator/",
    "L": "operator%", "M": "operator<", "N": "operator<=", "O": "operator>", "P": "operator>=",
    "Q": "operator,", "R": "operator()", "S": "operator~", "T": "operator^", "U": "operator|",
    "V": "operator&&", "W": "operator||", "X": "operator*=", "Y": "operator+=", "Z": "operator-=",
}
OPERATORS_US = {
    "0": "operator/=", "1": "operator%=", "2": "operator>>=", "3": "operator<<=", "4": "operator&=",
    "5": "operator|=", "6": "operator^=", "7": "vftable", "8": "vbtable", "9": "vcall",
    "A": "typeof", "B": "local static guard", "C": "string", "D": "vbase destructor",
    "E": "vector deleting destructor", "F": "default constructor closure",
    "G": "scalar deleting destructor", "H": "vector constructor iterator",
    "I": "vector destructor iterator", "J": "vector vbase constructor iterator",
    "K": "virtual displacement map", "L": "eh vector constructor iterator",
    "M": "eh vector destructor iterator", "N": "eh vector vbase constructor iterator",
    "O": "copy constructor closure", "R": "RTTI", "S": "local vftable", "T": "local vftable constructor closure",
    "U": "operator new[]", "V": "operator delete[]",
}
CALLCONV = {"A": "", "B": "", "C": "__pascal", "D": "__pascal", "E": "__thiscall", "F": "__thiscall",
            "G": "__stdcall", "H": "__stdcall", "I": "__fastcall", "J": "__fastcall"}
CV = {"A": "", "B": "const", "C": "volatile", "D": "const volatile"}


class Fail(Exception):
    pass


class Dem:
    def __init__(self, s):
        self.s = s
        self.i = 0
        self.names = []
        self.params = []
        self.unknown_args = False

    # ---- low level -----------------------------------------------------------------------
    def peek(self, n=1):
        return self.s[self.i:self.i + n]

    def take(self, n=1):
        r = self.s[self.i:self.i + n]
        if len(r) < n:
            raise Fail("eof")
        self.i += n
        return r

    def eat(self, t):
        if self.s.startswith(t, self.i):
            self.i += len(t)
            return True
        return False

    def number(self):
        """MSVC number: '?' prefix = negative; digits 0-9 mean 1-10; A-P hex nibbles ended by '@'."""
        neg = self.eat("?")
        c = self.take()
        if c.isdigit():
            v = int(c) + 1
        else:
            v = 0
            while c != "@":
                if not "A" <= c <= "P":
                    raise Fail("bad number")
                v = v * 16 + (ord(c) - ord("A"))
                c = self.take()
        return -v if neg else v

    # ---- names ---------------------------------------------------------------------------
    def memo_name(self, n):
        if len(self.names) < 10:
            self.names.append(n)
        return n

    def identifier(self):
        j = self.s.find("@", self.i)
        if j < 0 or j == self.i:
            raise Fail("identifier")
        n = self.s[self.i:j]
        self.i = j + 1
        return n

    def fragment(self, first=False):
        c = self.peek()
        if c.isdigit():
            self.i += 1
            if int(c) >= len(self.names):
                raise Fail("name backref")
            return self.names[int(c)]
        if self.peek(2) == "?$":
            self.i += 2
            return self.template()
        if c == "?":
            if self.peek(2) == "?A":          # anonymous namespace: ?A0xHASH@
                self.i += 2
                self.identifier()
                return "`anonymous namespace'"
            return self.special(first)
        return self.memo_name(self.identifier())

    def special(self, first):
        self.take()                     # '?'
        c = self.take()
        if c == "_":
            c2 = self.take()
            if c2 == "R":               # RTTI: ?_R0 type descriptor etc.
                k = self.take()
                return "<RTTI %s>" % k
            if c2 == "B" or c2 == "C":
                return "<%s>" % OPERATORS_US[c2]
            if c2 in OPERATORS_US:
                return "`%s'" % OPERATORS_US[c2]
            raise Fail("special _" + c2)
        if c in OPERATORS:
            return "<%s>" % OPERATORS[c]
        raise Fail("special " + c)

    def template(self):
        outer_names, outer_params = self.names, self.params
        self.names, self.params = [], []
        try:
            base = self.identifier()
            self.memo_name(base)
            args = []
            while not self.eat("@"):
                args.append(self.template_arg())
        finally:
            self.names, self.params = outer_names, outer_params
        full = "%s<%s>" % (base, ", ".join(args))
        return self.memo_name(full)

    def template_arg(self):
        if self.eat("$0"):
            return str(self.number())
        if self.eat("$S"):
            return ""
        if self.eat("$$T"):
            return "std::nullptr_t"
        if self.eat("$1") or self.eat("$H") or self.eat("$I") or self.eat("$J"):
            raise Fail("template pointer argument")
        if self.eat("$2"):
            raise Fail("template floating argument")
        if self.peek(2) == "$D" or self.peek(2) == "$E":
            raise Fail("template parameter index")
        return self.type()

    def qualified(self):
        """name@scope@...@@ : returns the components innermost first, as written."""
        comps = []
        first = True
        while not self.eat("@"):
            comps.append(self.fragment(first))
            first = False
        return comps

    def full_name(self, comps):
        """Components innermost-first -> (display name, class or None, kind)."""
        comps = list(comps)
        scope = comps[1:]
        head = comps[0]
        kind = "plain"
        if head.startswith("<") and head.endswith(">") and not head.startswith("<RTTI"):
            word = head[1:-1]
            if word == "ctor":
                head = scope[0].split("<")[0] if scope else "?"
                head = (scope[0] if scope else "?")
                kind = "ctor"
            elif word == "dtor":
                head = "~" + (scope[0] if scope else "?")
                kind = "dtor"
            else:
                head = word
                kind = "operator"
        elif head.startswith("`") and head.endswith("'"):
            kind = "special"
        elif head.startswith("operator"):
            kind = "operator"
        text = "::".join(list(reversed(scope)) + [head])
        cls = "::".join(reversed(scope)) if scope else None
        return text, cls, kind

    # ---- types ---------------------------------------------------------------------------
    def cvq(self):
        c = self.take()
        if c not in CV:
            raise Fail("cv")
        return CV[c]

    def qualified_type_name(self):
        comps = self.qualified()
        return "::".join(reversed(comps))

    def type(self):
        c = self.peek()
        if c == "":
            raise Fail("eof in type")
        if c in BUILTIN:
            self.i += 1
            return BUILTIN[c]
        if c == "_":
            self.i += 1
            c2 = self.take()
            if c2 in BUILTIN_US:
                return BUILTIN_US[c2]
            raise Fail("builtin _" + c2)
        if c in "PQRS":
            return self.pointer()
        if c in "AB":
            self.i += 1
            return self.pointer_tail("&", c)
        if self.peek(3) in ("$$Q", "$$R"):
            self.i += 3
            return self.pointer_tail("&&", "A")
        if self.peek(2) == "$$":
            if self.eat("$$C"):                  # cv-qualified type
                q = self.cvq()
                return (q + " " + self.type()).strip()
            raise Fail("$$ type " + self.peek(3))
        if c in "UVT":
            self.i += 1
            return {"U": "struct ", "V": "class ", "T": "union "}[c] + self.qualified_type_name()
        if c == "W":
            self.i += 1
            self.take()                           # underlying-type digit
            return "enum " + self.qualified_type_name()
        if c == "Y":
            self.i += 1
            n = self.number()
            dims = [self.number() for _ in range(n)]
            elem = self.type()
            return "%s %s" % (elem, "".join("[%d]" % d for d in dims))
        if c == "?":
            self.i += 1
            q = self.cvq()
            return (q + " " + self.type()).strip()
        if c.isdigit():
            self.i += 1
            if int(c) >= len(self.params):
                raise Fail("param backref")
            return self.params[int(c)]
        raise Fail("type " + c)

    def pointer(self):
        c = self.take()
        kind = {"P": "", "Q": "const", "R": "volatile", "S": "const volatile"}[c]
        if self.peek() == "6":                    # pointer to function
            self.i += 1
            return self.function_type(None, ptr=kind)
        if self.peek() == "8":
            raise Fail("pointer to member function")
        return self.pointer_tail("*", "A", kind)

    def pointer_tail(self, sym, c, kind=""):
        self.eat("E")                             # __ptr64
        q = self.cvq()
        inner = self.type()
        s = "%s %s" % (inner, sym) if not q else "%s %s %s" % (q, inner, sym)
        # keep "const T *" ordering
        if q:
            s = "%s %s %s" % (q, inner, sym)
        if kind:
            s += " " + kind
        return s

    def param_list(self):
        """Parameter types up to '@' or the varargs 'Z'.  Returns (list, variadic)."""
        if self.eat("X"):
            return [], False
        out = []
        while self.peek() not in ("@", "Z", ""):
            if self.peek(3) == "$$V":
                self.i += 3
                break
            start = self.i
            c = self.peek()
            if c.isdigit():
                self.i += 1
                if int(c) >= len(self.params):
                    raise Fail("param backref")
                out.append(self.params[int(c)])
                continue
            t = self.type()
            if self.i - start > 1 and len(self.params) < 10:
                self.params.append(t)
            out.append(t)
        if self.eat("@"):
            return out, False
        if self.eat("Z"):
            return out, True
        raise Fail("param list end")

    def function_type(self, this_info, ptr=None, structor=False):
        cc = self.take()
        if cc not in CALLCONV:
            raise Fail("callconv " + cc)
        if structor:
            if not self.eat("@"):
                raise Fail("structor return")
            ret = ""
        else:
            ret = self.return_type()
        params, var = self.param_list()
        self.eat("Z")                             # throw specification
        args = list(params) + (["..."] if var else [])
        conv = CALLCONV[cc]
        if ptr is not None:
            return "%s (%s*%s)(%s)" % (ret, (conv + " ") if conv else "", (" " + ptr) if ptr else "", ", ".join(args))
        return ret, args

    def return_type(self):
        if self.eat("?"):
            q = self.cvq()
            t = self.type()
            return (q + " " + t).strip()
        return self.type()


def demangle(name):
    """Returns a dict: ok, text, kind ('function'|'data'|'vftable'|'special'|'plain'), scope, member,
    access, static, virtual, const_this, ret, args (list of type strings), variadic."""
    if not name.startswith("?"):
        return {"ok": True, "text": name, "kind": "c", "scope": None, "member": name, "args": None, "ret": None,
                "access": None, "static": False, "virtual": False, "const_this": False, "variadic": False}
    d = Dem(name)
    info = {"ok": False, "text": name, "kind": "unknown", "scope": None, "member": None, "access": None,
            "static": False, "virtual": False, "const_this": False, "ret": None, "args": None, "variadic": False}
    try:
        d.take()
        comps = d.qualified()
        text, cls, nkind = d.full_name(comps)
        info.update(text=text, scope=cls, member=comps[0], name_kind=nkind)
        rest = d.s[d.i:]
        if not rest:
            info.update(ok=True, kind="plain")
            return info
        c = d.take()
        if c in "012" or c == "3":                # variable
            if c == "3" and False:
                pass
            t = d.type()
            d.take()                              # storage-class letter
            access = {"0": "private", "1": "protected", "2": "public", "3": "global"}[c]
            info.update(ok=True, kind="data", access=access, ret=t, static=c in "012")
            info["text"] = "%s %s" % (t, text)
            return info
        if c == "6" and nkind == "special":       # vftable: 6 <cv> {base@}* @
            q = d.cvq()
            info.update(ok=True, kind="vftable", text=text)
            return info
        if c in "ABCDEFIJKLMNQRSTUVYZ":
            access = "global" if c in "YZ" else ("private" if c in "ABCDEFGH" else "protected" if c in "IJKLMNOP" else "public")
            static = c in "CDKLST"
            virtual = c in "EFMNUV"
            is_member = c not in "YZ"
            if is_member and not static:
                d.eat("E")
                q = d.cvq()
                info["const_this"] = "const" in q
            structor = nkind in ("ctor", "dtor")
            ret, args = d.function_type(None, structor=structor)
            variadic = bool(args and args[-1] == "...")
            info.update(ok=True, kind="function", access=access, static=static and is_member, virtual=virtual,
                        ret=ret or None, args=args, variadic=variadic)
            sig = "%s%s%s(%s)%s" % ("static " if info["static"] else "", "virtual " if virtual else "",
                                    (ret + " ") if ret else "", ", ".join(args) if args else "void", " const" if info["const_this"] else "")
            # put the name inside
            info["text"] = "%s%s%s%s(%s)%s" % ("static " if info["static"] else "", "virtual " if virtual else "",
                                               (ret + " ") if ret else "", text, ", ".join(args), " const" if info["const_this"] else "")
            return info
        raise Fail("encoding " + c)
    except (Fail, IndexError, KeyError) as e:
        info["error"] = str(e)
        info["pos"] = d.i
        return info


SELFTEST = [
    ("?Start@CThread@@UEAA_N_KW4ThreadPriorityEnum_t@1@@Z",
     "virtual bool CThread::Start(unsigned __int64, enum CThread::ThreadPriorityEnum_t)"),
    ("??0CThreadEvent@@QEAA@_N@Z", "CThreadEvent::CThreadEvent(bool)"),
    ("??1CThread@@UEAA@XZ", "virtual CThread::~CThread()"),
    ("?ConMsg@@YAXPEBDZZ", "void ConMsg(const char *, ...)"),
    ("?GetThreadProc@CThread@@IEAAP6AKPEAX@ZXZ", "unsigned long (*)(void *) CThread::GetThreadProc()"),
    ("??4CCallStackStorage@@QEAAAEAV0@AEBV0@@Z",
     "class CCallStackStorage & CCallStackStorage::operator=(const class CCallStackStorage &)"),
    ("?g_nThreadID@@3V?$CThreadLocalInt@H@GenericThreadLocals@@A", "class GenericThreadLocals::CThreadLocalInt<int> g_nThreadID"),
    ("?JT_HelpWithJobTypes@@YA_NP6A_N_K@Z00@Z", None),
    ("??_7CThread@@6B@", "CThread::`vftable'"),
]


def selftest():
    bad = 0
    for m, want in SELFTEST:
        r = demangle(m)
        print("%-4s %s\n     -> %s%s" % ("ok" if r["ok"] else "FAIL", m, r["text"], "  [%s]" % r.get("error", "") if not r["ok"] else ""))
        if want is not None and r["text"] != want:
            print("     expected:", want)
            bad += 1
        if not r["ok"]:
            bad += 1
    return bad


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        sys.exit(1 if selftest() else 0)
    for a in sys.argv[1:]:
        print(demangle(a))
