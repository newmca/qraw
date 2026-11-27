"""A small JavaScript subset interpreter (Python 3 stdlib only).

Purpose: run draw.io's *sidebar definition* scripts (js/grapheditor/Sidebar.js,
js/diagramly/sidebar/Sidebar-*.js) against a recording host, so that palette
entries built by loops, closures, string arithmetic and cell builders are
captured as data. The scripts are only ever interpreted by this module; no
JavaScript engine is involved and the interpreter has no I/O, no network and
no `eval`: everything the scripts can reach is the host object table passed to
`Interpreter`.

Supported: var/let/const, function declarations and expressions (closures),
if/else, for, for-in, while, do-while, switch, break/continue/return,
try/catch/finally/throw, the usual operators, ?:, array and object literals,
`new` on host constructors, regular-expression literals for String.replace.
Not supported: classes, generators, async, destructuring, spread, template
literals, getters/setters. Anything outside the subset raises `Unsupported`
with a precise reason so the caller can report the entry instead of guessing.
"""

import math
import re


class Unsupported(Exception):
    """The script uses something outside the subset (or an unknown name)."""

    def __init__(self, message, pos=None):
        super().__init__(message)
        self.message = message
        self.pos = pos


class JSThrow(Exception):
    """A JavaScript `throw` that propagates through the interpreter."""

    def __init__(self, value):
        super().__init__(str(value))
        self.value = value


class _Return(Exception):
    def __init__(self, value):
        self.value = value


class _Break(Exception):
    pass


class _Continue(Exception):
    pass


class _Undefined:
    """JavaScript `undefined` (distinct from null, which is None)."""

    _instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    def __repr__(self):
        return "undefined"

    def __bool__(self):
        return False


UNDEF = _Undefined()


# --------------------------------------------------------------------------
# Tokenizer
# --------------------------------------------------------------------------

KEYWORDS = frozenset(
    "var let const function return if else for while do switch case default break continue "
    "new this null true false undefined typeof void delete in instanceof try catch finally throw".split())
# Keywords after which a '/' starts a regular expression, not a division.
_REGEX_AFTER_KEYWORD = frozenset("return typeof case in instanceof delete void else do".split())

_TOKEN = re.compile(r"""
    (?P<ws>\s+)
  | (?P<lcomment>//[^\n]*)
  | (?P<bcomment>/\*.*?\*/)
  | (?P<num>0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)
  | (?P<str>'(?:\\.|[^'\\\n])*'|"(?:\\.|[^"\\\n])*")
  | (?P<id>[A-Za-z_$][\w$]*)
  | (?P<punct>>>>=|===|!==|>>>|<<=|>>=|\+\+|--|==|!=|<=|>=|&&|\|\||\+=|-=|\*=|/=|%=|<<|>>|&=|\|=|\^=|[{}()\[\];,.<>+\-*/%&|^!~?:=])
""", re.S | re.X)

_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "b": "\b", "f": "\f", "v": "\v", "0": "\0"}


class Token:
    __slots__ = ("kind", "value", "line")

    def __init__(self, kind, value, line):
        self.kind = kind
        self.value = value
        self.line = line

    def __repr__(self):
        return "%s(%r)@%d" % (self.kind, self.value, self.line)


def unescape_js_string(body):
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        c = body[i]
        if c == "u" and re.match(r"[0-9a-fA-F]{4}", body[i + 1:i + 5]):
            out.append(chr(int(body[i + 1:i + 5], 16)))
            i += 5
        elif c == "x" and re.match(r"[0-9a-fA-F]{2}", body[i + 1:i + 3]):
            out.append(chr(int(body[i + 1:i + 3], 16)))
            i += 3
        elif c == "\n":
            i += 1  # line continuation
        else:
            out.append(_ESCAPES.get(c, c))
            i += 1
    return "".join(out)


def tokenize(src):
    tokens = []
    pos = 0
    line = 1
    n = len(src)
    while pos < n:
        m = _TOKEN.match(src, pos)
        if src[pos] == "/" and (m is None or m.lastgroup == "punct") and _regex_allowed(tokens):
            end = _scan_regex(src, pos)
            tokens.append(Token("regex", src[pos:end], line))
            pos = end
            continue
        if m is None:
            raise Unsupported("cannot tokenize %r at line %d" % (src[pos:pos + 20], line))
        text = m.group()
        kind = m.lastgroup
        if kind == "ws" or kind == "lcomment" or kind == "bcomment":
            line += text.count("\n")
        elif kind == "num":
            tokens.append(Token("num", float(int(text, 16)) if text[:2] in ("0x", "0X") else float(text), line))
        elif kind == "str":
            tokens.append(Token("str", unescape_js_string(text[1:-1]), line))
        elif kind == "id":
            tokens.append(Token("kw" if text in KEYWORDS else "id", text, line))
        else:
            tokens.append(Token("punct", text, line))
        pos = m.end()
    tokens.append(Token("eof", None, line))
    return tokens


def _regex_allowed(tokens):
    if not tokens:
        return True
    t = tokens[-1]
    if t.kind in ("num", "str", "id", "regex"):
        return False
    if t.kind == "kw":
        return t.value in _REGEX_AFTER_KEYWORD
    return t.value not in (")", "]", "}")


def _scan_regex(src, pos):
    i = pos + 1
    in_class = False
    while i < len(src):
        c = src[i]
        if c == "\\":
            i += 2
            continue
        if c == "[":
            in_class = True
        elif c == "]":
            in_class = False
        elif c == "/" and not in_class:
            i += 1
            while i < len(src) and (src[i].isalpha()):
                i += 1
            return i
        elif c == "\n":
            break
        i += 1
    raise Unsupported("unterminated regular expression")


# --------------------------------------------------------------------------
# Parser (AST = Node objects)
# --------------------------------------------------------------------------

class Node:
    def __init__(self, kind, pos, **fields):
        self.kind = kind
        self.pos = pos  # (file, line)
        self.__dict__.update(fields)

    def __repr__(self):
        return "Node(%s)" % self.kind


_BINARY_PRECEDENCE = {
    "||": 1, "&&": 2, "|": 3, "^": 4, "&": 5,
    "==": 6, "!=": 6, "===": 6, "!==": 6,
    "<": 7, ">": 7, "<=": 7, ">=": 7, "instanceof": 7, "in": 7,
    "<<": 8, ">>": 8, ">>>": 8,
    "+": 9, "-": 9, "*": 10, "/": 10, "%": 10,
}
_ASSIGN_OPS = frozenset(("=", "+=", "-=", "*=", "/=", "%=", "|=", "&=", "^=", "<<=", ">>="))


class Parser:
    def __init__(self, src, filename):
        self.file = filename
        self.tokens = tokenize(src)
        self.i = 0

    # token helpers
    @property
    def tok(self):
        return self.tokens[self.i]

    def pos(self):
        return (self.file, self.tok.line)

    def is_punct(self, value):
        t = self.tok
        return t.kind == "punct" and t.value == value

    def is_kw(self, value):
        t = self.tok
        return t.kind == "kw" and t.value == value

    def accept_punct(self, value):
        if self.is_punct(value):
            self.i += 1
            return True
        return False

    def expect_punct(self, value):
        if not self.accept_punct(value):
            raise Unsupported("expected %r but found %r" % (value, self.tok.value), self.pos())

    def expect_kw(self, value):
        if not self.is_kw(value):
            raise Unsupported("expected %r but found %r" % (value, self.tok.value), self.pos())
        self.i += 1

    def identifier(self):
        t = self.tok
        if t.kind not in ("id", "kw"):
            raise Unsupported("expected identifier but found %r" % (t.value,), self.pos())
        self.i += 1
        return t.value

    def consume_semicolon(self):
        self.accept_punct(";")  # automatic semicolon insertion: a missing ';' is fine

    # program / statements
    def parse_program(self):
        body = []
        while self.tok.kind != "eof":
            body.append(self.statement())
        return Node("block", (self.file, 1), body=body)

    def statement(self):
        t = self.tok
        pos = self.pos()
        if t.kind == "punct":
            if t.value == "{":
                return self.block()
            if t.value == ";":
                self.i += 1
                return Node("empty", pos)
        if t.kind == "kw":
            handler = getattr(self, "stmt_" + t.value, None)
            if handler is not None:
                return handler()
        expr = self.expression()
        self.consume_semicolon()
        return Node("expr", pos, expr=expr)

    def block(self):
        pos = self.pos()
        self.expect_punct("{")
        body = []
        while not self.is_punct("}"):
            if self.tok.kind == "eof":
                raise Unsupported("unterminated block", pos)
            body.append(self.statement())
        self.i += 1
        return Node("block", pos, body=body)

    def var_declarations(self, allow_in=True):
        pos = self.pos()
        self.i += 1  # var / let / const
        decls = []
        while True:
            name = self.identifier()
            init = None
            if self.accept_punct("="):
                init = self.assignment(allow_in)
            decls.append((name, init))
            if not self.accept_punct(","):
                break
        return Node("var", pos, decls=decls)

    def stmt_var(self):
        node = self.var_declarations()
        self.consume_semicolon()
        return node

    stmt_let = stmt_var
    stmt_const = stmt_var

    def stmt_function(self):
        pos = self.pos()
        self.i += 1
        name = self.identifier()
        return Node("funcdecl", pos, name=name, func=self.function_rest(name, pos))

    def function_rest(self, name, pos):
        self.expect_punct("(")
        params = []
        while not self.is_punct(")"):
            params.append(self.identifier())
            if not self.accept_punct(","):
                break
        self.expect_punct(")")
        body = self.block()
        return Node("function", pos, name=name, params=params, body=body)

    def stmt_if(self):
        pos = self.pos()
        self.i += 1
        self.expect_punct("(")
        test = self.expression()
        self.expect_punct(")")
        then = self.statement()
        other = None
        if self.is_kw("else"):
            self.i += 1
            other = self.statement()
        return Node("if", pos, test=test, then=then, other=other)

    def stmt_for(self):
        pos = self.pos()
        self.i += 1
        self.expect_punct("(")
        init = None
        if not self.is_punct(";"):
            if self.tok.kind == "kw" and self.tok.value in ("var", "let", "const"):
                init = self.var_declarations(allow_in=False)
            else:
                init = Node("expr", self.pos(), expr=self.expression(allow_in=False))
            if self.is_kw("in"):
                self.i += 1
                obj = self.expression()
                self.expect_punct(")")
                body = self.statement()
                if init.kind == "var":
                    target = init.decls[0][0]
                    declare = True
                else:
                    target = init.expr
                    declare = False
                return Node("forin", pos, target=target, declare=declare, obj=obj, body=body)
        self.expect_punct(";")
        test = None if self.is_punct(";") else self.expression()
        self.expect_punct(";")
        update = None if self.is_punct(")") else self.expression()
        self.expect_punct(")")
        body = self.statement()
        return Node("for", pos, init=init, test=test, update=update, body=body)

    def stmt_while(self):
        pos = self.pos()
        self.i += 1
        self.expect_punct("(")
        test = self.expression()
        self.expect_punct(")")
        return Node("while", pos, test=test, body=self.statement())

    def stmt_do(self):
        pos = self.pos()
        self.i += 1
        body = self.statement()
        self.expect_kw("while")
        self.expect_punct("(")
        test = self.expression()
        self.expect_punct(")")
        self.consume_semicolon()
        return Node("dowhile", pos, test=test, body=body)

    def stmt_return(self):
        pos = self.pos()
        line = self.tok.line
        self.i += 1
        value = None
        if not (self.is_punct(";") or self.is_punct("}") or self.tok.kind == "eof" or self.tok.line != line):
            value = self.expression()
        self.consume_semicolon()
        return Node("return", pos, value=value)

    def stmt_break(self):
        pos = self.pos()
        self.i += 1
        self.consume_semicolon()
        return Node("break", pos)

    def stmt_continue(self):
        pos = self.pos()
        self.i += 1
        self.consume_semicolon()
        return Node("continue", pos)

    def stmt_throw(self):
        pos = self.pos()
        self.i += 1
        value = self.expression()
        self.consume_semicolon()
        return Node("throw", pos, value=value)

    def stmt_try(self):
        pos = self.pos()
        self.i += 1
        block = self.block()
        param = None
        handler = None
        final = None
        if self.is_kw("catch"):
            self.i += 1
            self.expect_punct("(")
            param = self.identifier()
            self.expect_punct(")")
            handler = self.block()
        if self.is_kw("finally"):
            self.i += 1
            final = self.block()
        return Node("try", pos, block=block, param=param, handler=handler, final=final)

    def stmt_switch(self):
        pos = self.pos()
        self.i += 1
        self.expect_punct("(")
        disc = self.expression()
        self.expect_punct(")")
        self.expect_punct("{")
        cases = []
        while not self.is_punct("}"):
            if self.is_kw("case"):
                self.i += 1
                test = self.expression()
            else:
                self.expect_kw("default")
                test = None
            self.expect_punct(":")
            body = []
            while not (self.is_punct("}") or self.is_kw("case") or self.is_kw("default")):
                body.append(self.statement())
            cases.append((test, body))
        self.i += 1
        return Node("switch", pos, disc=disc, cases=cases)

    # expressions
    def expression(self, allow_in=True):
        pos = self.pos()
        expr = self.assignment(allow_in)
        if self.is_punct(","):
            items = [expr]
            while self.accept_punct(","):
                items.append(self.assignment(allow_in))
            return Node("seq", pos, items=items)
        return expr

    def assignment(self, allow_in=True):
        pos = self.pos()
        left = self.conditional(allow_in)
        t = self.tok
        if t.kind == "punct" and t.value in _ASSIGN_OPS:
            if left.kind not in ("id", "member", "index"):
                raise Unsupported("invalid assignment target", pos)
            self.i += 1
            right = self.assignment(allow_in)
            return Node("assign", pos, op=t.value, target=left, value=right)
        return left

    def conditional(self, allow_in):
        pos = self.pos()
        test = self.binary(1, allow_in)
        if self.accept_punct("?"):
            then = self.assignment()
            self.expect_punct(":")
            other = self.assignment(allow_in)
            return Node("cond", pos, test=test, then=then, other=other)
        return test

    def binary(self, min_prec, allow_in):
        left = self.unary()
        while True:
            t = self.tok
            op = t.value if t.kind in ("punct", "kw") else None
            prec = _BINARY_PRECEDENCE.get(op)
            if prec is None or prec < min_prec or (op == "in" and not allow_in):
                return left
            if t.kind == "kw" and op not in ("in", "instanceof"):
                return left
            pos = self.pos()
            self.i += 1
            right = self.binary(prec + 1, allow_in)
            if op in ("||", "&&"):
                left = Node("logical", pos, op=op, left=left, right=right)
            else:
                left = Node("binary", pos, op=op, left=left, right=right)

    def unary(self):
        t = self.tok
        pos = self.pos()
        if t.kind == "punct" and t.value in ("!", "-", "+", "~"):
            self.i += 1
            return Node("unary", pos, op=t.value, operand=self.unary())
        if t.kind == "punct" and t.value in ("++", "--"):
            self.i += 1
            return Node("update", pos, op=t.value, prefix=True, target=self.unary())
        if t.kind == "kw" and t.value in ("typeof", "void", "delete"):
            self.i += 1
            return Node("unary", pos, op=t.value, operand=self.unary())
        expr = self.postfix()
        t = self.tok
        if t.kind == "punct" and t.value in ("++", "--") and t.line == self.tokens[self.i - 1].line:
            self.i += 1
            return Node("update", pos, op=t.value, prefix=False, target=expr)
        return expr

    def postfix(self):
        pos = self.pos()
        if self.is_kw("new"):
            self.i += 1
            callee = self.member_only()
            args = self.arguments() if self.is_punct("(") else []
            expr = Node("new", pos, callee=callee, args=args)
        else:
            expr = self.primary()
        return self.suffixes(expr, allow_calls=True)

    def member_only(self):
        expr = self.primary()
        return self.suffixes(expr, allow_calls=False)

    def suffixes(self, expr, allow_calls):
        while True:
            pos = self.pos()
            if self.accept_punct("."):
                expr = Node("member", pos, obj=expr, name=self.identifier())
            elif self.is_punct("["):
                self.i += 1
                index = self.expression()
                self.expect_punct("]")
                expr = Node("index", pos, obj=expr, index=index)
            elif allow_calls and self.is_punct("("):
                expr = Node("call", pos, callee=expr, args=self.arguments())
            else:
                return expr

    def arguments(self):
        self.expect_punct("(")
        args = []
        while not self.is_punct(")"):
            args.append(self.assignment())
            if not self.accept_punct(","):
                break
        self.expect_punct(")")
        return args

    def primary(self):
        t = self.tok
        pos = self.pos()
        if t.kind == "num" or t.kind == "str":
            self.i += 1
            return Node("lit", pos, value=t.value)
        if t.kind == "regex":
            self.i += 1
            return Node("regex", pos, text=t.value)
        if t.kind == "id":
            self.i += 1
            return Node("id", pos, name=t.value)
        if t.kind == "kw":
            if t.value in ("null", "true", "false", "undefined"):
                self.i += 1
                return Node("lit", pos, value={"null": None, "true": True, "false": False, "undefined": UNDEF}[t.value])
            if t.value == "this":
                self.i += 1
                return Node("this", pos)
            if t.value == "function":
                self.i += 1
                name = self.identifier() if self.tok.kind == "id" else None
                return self.function_rest(name, pos)
        if t.kind == "punct":
            if t.value == "(":
                self.i += 1
                expr = self.expression()
                self.expect_punct(")")
                return expr
            if t.value == "[":
                return self.array_literal()
            if t.value == "{":
                return self.object_literal()
        raise Unsupported("unexpected token %r" % (t.value,), pos)

    def array_literal(self):
        pos = self.pos()
        self.expect_punct("[")
        items = []
        while not self.is_punct("]"):
            if self.is_punct(","):  # elision
                self.i += 1
                items.append(Node("lit", self.pos(), value=UNDEF))
                continue
            items.append(self.assignment())
            if not self.accept_punct(","):
                break
        self.expect_punct("]")
        return Node("array", pos, items=items)

    def object_literal(self):
        pos = self.pos()
        self.expect_punct("{")
        props = []
        while not self.is_punct("}"):
            t = self.tok
            if t.kind in ("id", "kw", "str"):
                key = t.value
            elif t.kind == "num":
                key = to_str(t.value)
            else:
                raise Unsupported("bad object key %r" % (t.value,), pos)
            self.i += 1
            self.expect_punct(":")
            props.append((key, self.assignment()))
            if not self.accept_punct(","):
                break
        self.expect_punct("}")
        return Node("object", pos, props=props)


def parse(src, filename):
    return Parser(src, filename).parse_program()


# --------------------------------------------------------------------------
# Values and conversions
# --------------------------------------------------------------------------

class JSFunction:
    def __init__(self, node, scope, interp):
        self.node = node
        self.scope = scope
        self.interp = interp
        self.props = {}

    def call(self, this, args):
        return self.interp.call_function(self, this, args)


class NativeFn:
    """A host function: fn(this, args) -> value."""

    def __init__(self, fn, name=""):
        self.fn = fn
        self.name = name

    def call(self, this, args):
        return self.fn(this, args)


class HostObject:
    """Base class of host objects: `js_get`/`js_set` by property name.

    Methods are Python methods named `m_<jsname>(self, args)`.
    """

    def js_get(self, name):
        method = getattr(self, "m_" + name, None)
        if method is not None:
            return NativeFn(lambda this, args, _m=method: _m(args), name)
        return self.js_prop(name)

    def js_prop(self, name):
        return self.__dict__.get("p_" + name, UNDEF)

    def js_set(self, name, value):
        self.__dict__["p_" + name] = value


def to_str(v):
    if isinstance(v, str):
        return v
    if v is None:
        return "null"
    if v is UNDEF:
        return "undefined"
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, (int, float)):
        return number_to_str(v)
    if isinstance(v, list):
        return ",".join("" if x is None or x is UNDEF else to_str(x) for x in v)
    if isinstance(v, dict):
        return "[object Object]"
    return "[object]"


def number_to_str(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        if math.isnan(v):
            return "NaN"
        if math.isinf(v):
            return "Infinity" if v > 0 else "-Infinity"
        if v == int(v) and abs(v) < 1e21:
            return str(int(v))
        text = repr(v)
        if "e" in text:
            mant, exp = text.split("e")
            exp_i = int(exp)
            if -7 < exp_i < 21:
                from decimal import Decimal
                return format(Decimal(text), "f")
            return "%se%s%d" % (mant, "+" if exp_i > 0 else "-", abs(exp_i))
        return text
    return str(v)


def to_num(v):
    if isinstance(v, bool):
        return 1.0 if v else 0.0
    if isinstance(v, (int, float)):
        return float(v)
    if v is None:
        return 0.0
    if v is UNDEF:
        return math.nan
    if isinstance(v, str):
        s = v.strip()
        if s == "":
            return 0.0
        try:
            return float(int(s, 16)) if s[:2] in ("0x", "0X") else float(s)
        except ValueError:
            return math.nan
    if isinstance(v, list):
        return to_num(to_str(v)) if len(v) <= 1 else math.nan
    return math.nan


def truthy(v):
    if v is None or v is UNDEF or v is False:
        return False
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return not (v == 0 or v != v)
    if isinstance(v, str):
        return v != ""
    return True


def strict_equals(a, b):
    if isinstance(a, bool) != isinstance(b, bool):
        return False
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a == b
    if isinstance(a, str) and isinstance(b, str):
        return a == b
    if a is None or a is UNDEF or b is None or b is UNDEF:
        return a is b
    return a is b


def loose_equals(a, b):
    nullish = (None, UNDEF)
    if (a in nullish if not isinstance(a, (list, dict)) else False) or \
            (b in nullish if not isinstance(b, (list, dict)) else False):
        return (a is None or a is UNDEF) and (b is None or b is UNDEF)
    if type(a) is type(b) or (isinstance(a, (int, float)) and isinstance(b, (int, float)) and
                              not isinstance(a, bool) and not isinstance(b, bool)):
        return strict_equals(a, b)
    if isinstance(a, (str, int, float, bool)) and isinstance(b, (str, int, float, bool)):
        return to_num(a) == to_num(b)
    return a is b


def js_add(a, b):
    if isinstance(a, (list, dict)) or isinstance(b, (list, dict)) or isinstance(a, str) or isinstance(b, str):
        return to_str(a) + to_str(b)
    return to_num(a) + to_num(b)


def to_int32(v):
    n = to_num(v)
    if n != n or n in (math.inf, -math.inf):
        return 0
    n = int(n) & 0xFFFFFFFF
    return n - 0x100000000 if n >= 0x80000000 else n


# --------------------------------------------------------------------------
# Built-in methods of primitives
# --------------------------------------------------------------------------

def _js_regex(text):
    m = re.match(r"^/(.*)/([a-z]*)$", text, re.S)
    body, flags = m.group(1), m.group(2)
    pyflags = re.I if "i" in flags else 0
    return re.compile(body, pyflags), "g" in flags


def _js_replacement(repl):
    # JS $1 -> Python \g<1>; '$&' -> whole match.
    return re.sub(r"\$(\d|&)", lambda m: "\\g<0>" if m.group(1) == "&" else "\\g<%s>" % m.group(1),
                  repl.replace("\\", "\\\\"))


def _string_replace(s, args):
    pattern = args[0]
    repl = args[1] if len(args) > 1 else UNDEF
    if isinstance(pattern, Regex):
        rx, global_ = pattern.rx, pattern.global_
        if isinstance(repl, (JSFunction, NativeFn)):
            raise Unsupported("String.replace with a function replacement")
        return rx.sub(_js_replacement(to_str(repl)), s, count=0 if global_ else 1)
    return s.replace(to_str(pattern), to_str(repl), 1)


class Regex:
    def __init__(self, text):
        self.rx, self.global_ = _js_regex(text)


def _str_substring(s, args):
    n = len(s)
    start = int(max(0, min(n, to_num(args[0])))) if args and to_num(args[0]) == to_num(args[0]) else 0
    end = n if len(args) < 2 or args[1] is UNDEF else int(max(0, min(n, to_num(args[1]))))
    if start > end:
        start, end = end, start
    return s[start:end]


def _norm_index(v, n, default):
    if v is UNDEF:
        return default
    i = to_num(v)
    if i != i:
        return 0
    i = int(i)
    return max(0, n + i) if i < 0 else min(i, n)


def _str_split(s, args):
    sep = args[0] if args else UNDEF
    if sep is UNDEF:
        return [s]
    if isinstance(sep, Regex):
        return re.split(sep.rx.pattern, s, flags=sep.rx.flags)
    sep = to_str(sep)
    return list(s) if sep == "" else s.split(sep)


_STRING_METHODS = {
    "toLowerCase": lambda s, a: s.lower(),
    "toUpperCase": lambda s, a: s.upper(),
    "trim": lambda s, a: s.strip(),
    "replace": _string_replace,
    "split": _str_split,
    "indexOf": lambda s, a: float(s.find(to_str(a[0]), int(to_num(a[1])) if len(a) > 1 else 0)),
    "lastIndexOf": lambda s, a: float(s.rfind(to_str(a[0]))),
    "substring": _str_substring,
    "substr": lambda s, a: s[_norm_index(a[0], len(s), 0):][:(len(s) if len(a) < 2 else int(to_num(a[1])))],
    "slice": lambda s, a: s[_norm_index(a[0], len(s), 0):_norm_index(a[1] if len(a) > 1 else UNDEF, len(s), len(s))],
    "charAt": lambda s, a: s[int(to_num(a[0]))] if 0 <= to_num(a[0]) < len(s) else "",
    "charCodeAt": lambda s, a: float(ord(s[int(to_num(a[0]))])) if 0 <= to_num(a[0]) < len(s) else math.nan,
    "concat": lambda s, a: s + "".join(to_str(x) for x in a),
    "toString": lambda s, a: s,
    "startsWith": lambda s, a: s.startswith(to_str(a[0])),
    "endsWith": lambda s, a: s.endswith(to_str(a[0])),
}

def _array_splice(arr, args):
    n = len(arr)
    start = _norm_index(args[0], n, 0)
    count = n - start if len(args) < 2 else max(0, min(n - start, int(to_num(args[1]))))
    removed = arr[start:start + count]
    arr[start:start + count] = list(args[2:])
    return removed


def _array_index_of(arr, args):
    for i, x in enumerate(arr):
        if strict_equals(x, args[0]):
            return float(i)
    return -1.0


def _array_push(arr, args):
    arr.extend(args)
    return float(len(arr))


def _array_concat(arr, args):
    out = list(arr)
    for a in args:
        out.extend(a) if isinstance(a, list) else out.append(a)
    return out


_ARRAY_METHODS = {
    "push": _array_push,
    "pop": lambda a, args: a.pop() if a else UNDEF,
    "shift": lambda a, args: a.pop(0) if a else UNDEF,
    "unshift": lambda a, args: (a.__setitem__(slice(0, 0), list(args)), float(len(a)))[1],
    "concat": _array_concat,
    "slice": lambda a, args: a[_norm_index(args[0] if args else UNDEF, len(a), 0):
                                _norm_index(args[1] if len(args) > 1 else UNDEF, len(a), len(a))],
    "splice": _array_splice,
    "indexOf": _array_index_of,
    "join": lambda a, args: (to_str(args[0]) if args and args[0] is not UNDEF else ",").join(
        "" if x is None or x is UNDEF else to_str(x) for x in a),
    "reverse": lambda a, args: (a.reverse(), a)[1],
    "toString": lambda a, args: to_str(a),
}

_NUMBER_METHODS = {
    "toString": lambda n, a: to_str(n),
    "toFixed": lambda n, a: "%.*f" % (int(to_num(a[0])) if a else 0, n),
}


# --------------------------------------------------------------------------
# Interpreter
# --------------------------------------------------------------------------

class Scope:
    __slots__ = ("vars", "parent", "this")

    def __init__(self, parent=None, this=UNDEF):
        self.vars = {}
        self.parent = parent
        self.this = this

    def lookup_scope(self, name):
        s = self
        while s is not None:
            if name in s.vars:
                return s
            s = s.parent
        return None


class Interpreter:
    """Tree-walking evaluator.

    `globals` maps global names to values (host objects, functions, numbers).
    `on_element_error(node, exc)` is called when an *array literal element*
    raises `Unsupported`; the element is then dropped (the host records the
    skipped entry). This is how one unresolvable palette entry does not take
    down the 1,000 siblings in the same array.
    """

    MAX_CALL_DEPTH = 150

    def __init__(self, globals_, on_element_error=None):
        self.global_scope = Scope()
        self.global_scope.vars.update(globals_)
        self.on_element_error = on_element_error
        self.depth = 0

    # -- entry points
    def run(self, program, scope=None):
        scope = scope or self.global_scope
        self.hoist(program.body, scope)
        for stmt in program.body:
            self.exec(stmt, scope)

    def call_value(self, fn, this, args, pos=None):
        if isinstance(fn, (JSFunction, NativeFn)):
            return fn.call(this, args)
        if callable(getattr(fn, "js_call", None)):
            return fn.js_call(args)
        raise Unsupported("call of a non-function value", pos)

    def call_function(self, fn, this, args):
        node = fn.node
        self.depth += 1
        if self.depth > self.MAX_CALL_DEPTH:
            self.depth -= 1
            raise Unsupported("call depth exceeded", node.pos)
        try:
            scope = Scope(fn.scope, this)
            for i, p in enumerate(node.params):
                scope.vars[p] = args[i] if i < len(args) else UNDEF
            scope.vars["arguments"] = list(args)
            self.hoist(node.body.body, scope)
            try:
                for stmt in node.body.body:
                    self.exec(stmt, scope)
            except _Return as r:
                return r.value
            return UNDEF
        finally:
            self.depth -= 1

    # -- statements
    def hoist(self, body, scope):
        for stmt in body:
            if stmt.kind == "funcdecl":
                scope.vars[stmt.name] = JSFunction(stmt.func, scope, self)
            elif stmt.kind == "var":
                for name, _ in stmt.decls:
                    scope.vars.setdefault(name, UNDEF)

    def exec(self, node, scope):
        handler = getattr(self, "x_" + node.kind)
        handler(node, scope)

    def x_empty(self, node, scope):
        pass

    def x_funcdecl(self, node, scope):
        pass  # hoisted

    def x_expr(self, node, scope):
        self.eval(node.expr, scope)

    def x_block(self, node, scope):
        self.hoist(node.body, scope)
        for stmt in node.body:
            self.exec(stmt, scope)

    def x_var(self, node, scope):
        for name, init in node.decls:
            if init is not None:
                scope.vars[name] = self.eval(init, scope)
            elif name not in scope.vars:
                scope.vars[name] = UNDEF

    def x_if(self, node, scope):
        if truthy(self.eval(node.test, scope)):
            self.exec(node.then, scope)
        elif node.other is not None:
            self.exec(node.other, scope)

    def x_for(self, node, scope):
        if node.init is not None:
            self.exec(node.init, scope)
        while node.test is None or truthy(self.eval(node.test, scope)):
            try:
                self.exec(node.body, scope)
            except _Break:
                break
            except _Continue:
                pass
            if node.update is not None:
                self.eval(node.update, scope)

    def x_forin(self, node, scope):
        obj = self.eval(node.obj, scope)
        if isinstance(obj, dict):
            keys = list(obj.keys())
        elif isinstance(obj, list):
            keys = [str(i) for i in range(len(obj))]
        elif isinstance(obj, HostObject) and hasattr(obj, "js_keys"):
            keys = list(obj.js_keys())
        else:
            raise Unsupported("for-in over unsupported value", node.pos)
        for key in keys:
            if node.declare:
                scope.vars[node.target] = key
            else:
                self.assign_to(node.target, key, scope)
            try:
                self.exec(node.body, scope)
            except _Break:
                break
            except _Continue:
                continue

    def x_while(self, node, scope):
        while truthy(self.eval(node.test, scope)):
            try:
                self.exec(node.body, scope)
            except _Break:
                break
            except _Continue:
                continue

    def x_dowhile(self, node, scope):
        while True:
            try:
                self.exec(node.body, scope)
            except _Break:
                break
            except _Continue:
                pass
            if not truthy(self.eval(node.test, scope)):
                break

    def x_return(self, node, scope):
        raise _Return(UNDEF if node.value is None else self.eval(node.value, scope))

    def x_break(self, node, scope):
        raise _Break()

    def x_continue(self, node, scope):
        raise _Continue()

    def x_throw(self, node, scope):
        raise JSThrow(self.eval(node.value, scope))

    def x_try(self, node, scope):
        try:
            try:
                self.exec(node.block, scope)
            except JSThrow as e:
                if node.handler is None:
                    raise
                scope.vars[node.param] = e.value
                self.exec(node.handler, scope)
        finally:
            if node.final is not None:
                self.exec(node.final, scope)

    def x_switch(self, node, scope):
        disc = self.eval(node.disc, scope)
        matched = False
        try:
            for test, body in node.cases:
                if not matched and test is not None and strict_equals(disc, self.eval(test, scope)):
                    matched = True
                if matched:
                    for stmt in body:
                        self.exec(stmt, scope)
            if not matched:
                started = False
                for test, body in node.cases:
                    if test is None:
                        started = True
                    if started:
                        for stmt in body:
                            self.exec(stmt, scope)
        except _Break:
            pass

    # -- expressions
    def eval(self, node, scope):
        return getattr(self, "e_" + node.kind)(node, scope)

    def e_lit(self, node, scope):
        return node.value

    def e_regex(self, node, scope):
        return Regex(node.text)

    def e_this(self, node, scope):
        s = scope
        while s is not None:
            if s.this is not UNDEF:
                return s.this
            s = s.parent
        return UNDEF

    def e_id(self, node, scope):
        s = scope.lookup_scope(node.name)
        if s is None:
            raise Unsupported("unresolved identifier %s" % node.name, node.pos)
        return s.vars[node.name]

    def e_seq(self, node, scope):
        value = UNDEF
        for item in node.items:
            value = self.eval(item, scope)
        return value

    def e_array(self, node, scope):
        out = []
        for item in node.items:
            try:
                out.append(self.eval(item, scope))
            except Unsupported as exc:
                if self.on_element_error is None:
                    raise
                self.on_element_error(item, exc)
        return out

    def e_object(self, node, scope):
        return {key: self.eval(value, scope) for key, value in node.props}

    def e_function(self, node, scope):
        return JSFunction(node, scope, self)

    def e_cond(self, node, scope):
        return self.eval(node.then if truthy(self.eval(node.test, scope)) else node.other, scope)

    def e_logical(self, node, scope):
        left = self.eval(node.left, scope)
        if node.op == "||":
            return left if truthy(left) else self.eval(node.right, scope)
        return self.eval(node.right, scope) if truthy(left) else left

    def e_unary(self, node, scope):
        op = node.op
        if op == "typeof":
            try:
                v = self.eval(node.operand, scope)
            except Unsupported:
                if node.operand.kind == "id":
                    return "undefined"
                raise
            return self.type_of(v)
        v = self.eval(node.operand, scope)
        if op == "!":
            return not truthy(v)
        if op == "-":
            return -to_num(v)
        if op == "+":
            return to_num(v)
        if op == "~":
            return float(~to_int32(v))
        if op == "void":
            return UNDEF
        raise Unsupported("unary %s" % op, node.pos)

    @staticmethod
    def type_of(v):
        if v is UNDEF:
            return "undefined"
        if v is None:
            return "object"
        if isinstance(v, bool):
            return "boolean"
        if isinstance(v, (int, float)):
            return "number"
        if isinstance(v, str):
            return "string"
        if isinstance(v, (JSFunction, NativeFn)):
            return "function"
        return "object"

    def e_binary(self, node, scope):
        op = node.op
        a = self.eval(node.left, scope)
        b = self.eval(node.right, scope)
        return self.binary_op(op, a, b, node)

    def binary_op(self, op, a, b, node):
        if op == "+":
            return js_add(a, b)
        if op == "-":
            return to_num(a) - to_num(b)
        if op == "*":
            return to_num(a) * to_num(b)
        if op == "/":
            x, y = to_num(a), to_num(b)
            if y == 0:
                return math.nan if x == 0 or x != x else math.copysign(math.inf, x) * (math.copysign(1, y))
            return x / y
        if op == "%":
            x, y = to_num(a), to_num(b)
            return math.nan if y == 0 else math.fmod(x, y)
        if op == "===":
            return strict_equals(a, b)
        if op == "!==":
            return not strict_equals(a, b)
        if op == "==":
            return loose_equals(a, b)
        if op == "!=":
            return not loose_equals(a, b)
        if op in ("<", ">", "<=", ">="):
            if isinstance(a, str) and isinstance(b, str):
                x, y = a, b
            else:
                x, y = to_num(a), to_num(b)
                if x != x or y != y:
                    return False
            return {"<": x < y, ">": x > y, "<=": x <= y, ">=": x >= y}[op]
        if op == "|":
            return float(to_int32(a) | to_int32(b))
        if op == "&":
            return float(to_int32(a) & to_int32(b))
        if op == "^":
            return float(to_int32(a) ^ to_int32(b))
        if op == "<<":
            return float(to_int32(to_int32(a) << (to_int32(b) & 31)))
        if op == ">>":
            return float(to_int32(a) >> (to_int32(b) & 31))
        if op == "in":
            key = to_str(a)
            if isinstance(b, dict):
                return key in b
            raise Unsupported("'in' on unsupported value", node.pos)
        raise Unsupported("operator %s" % op, node.pos)

    def e_assign(self, node, scope):
        if node.op == "=":
            value = self.eval(node.value, scope)
        else:
            current = self.eval(node.target, scope)
            value = self.binary_op(node.op[:-1], current, self.eval(node.value, scope), node)
        self.assign_to(node.target, value, scope, node)
        return value

    def e_update(self, node, scope):
        old = to_num(self.eval(node.target, scope))
        new = old + (1 if node.op == "++" else -1)
        self.assign_to(node.target, new, scope, node)
        return new if node.prefix else old

    def assign_to(self, target, value, scope, node=None):
        if isinstance(target, str):
            scope.vars[target] = value
            return
        kind = target.kind
        if kind == "id":
            s = scope.lookup_scope(target.name)
            (s or self.global_scope).vars[target.name] = value
        elif kind == "member":
            self.set_member(self.eval(target.obj, scope), target.name, value, target)
        elif kind == "index":
            self.set_member(self.eval(target.obj, scope), self.key_of(self.eval(target.index, scope)), value, target)
        else:
            raise Unsupported("invalid assignment target", target.pos)

    @staticmethod
    def key_of(v):
        if isinstance(v, float) and v == int(v):
            return int(v)
        return v if isinstance(v, int) and not isinstance(v, bool) else to_str(v)

    def e_member(self, node, scope):
        return self.get_member(self.eval(node.obj, scope), node.name, node)

    def e_index(self, node, scope):
        return self.get_member(self.eval(node.obj, scope), self.key_of(self.eval(node.index, scope)), node)

    def e_call(self, node, scope):
        callee = node.callee
        if callee.kind in ("member", "index"):
            this = self.eval(callee.obj, scope)
            name = callee.name if callee.kind == "member" else self.key_of(self.eval(callee.index, scope))
            args = [self.eval(a, scope) for a in node.args]
            return self.invoke_method(this, name, args, node)
        fn = self.eval(callee, scope)
        args = [self.eval(a, scope) for a in node.args]
        return self.call_value(fn, UNDEF, args, node.pos)

    def e_new(self, node, scope):
        ctor = self.eval(node.callee, scope)
        args = [self.eval(a, scope) for a in node.args]
        construct = getattr(ctor, "js_new", None)
        if construct is None:
            raise Unsupported("new on unsupported constructor", node.pos)
        return construct(args)

    # -- member access
    def invoke_method(self, this, name, args, node):
        if isinstance(this, str):
            fn = _STRING_METHODS.get(name)
            if fn is None:
                raise Unsupported("string method %s" % name, node.pos)
            return fn(this, args)
        if isinstance(this, list) and isinstance(name, int):
            return self.call_value(self.get_member(this, name, node), this, args, node.pos)
        if isinstance(this, list):
            fn = _ARRAY_METHODS.get(name)
            if fn is None:
                raise Unsupported("array method %s" % name, node.pos)
            return fn(this, args)
        if isinstance(this, (int, float)) and not isinstance(this, bool):
            fn = _NUMBER_METHODS.get(name)
            if fn is None:
                raise Unsupported("number method %s" % name, node.pos)
            return fn(this, args)
        if isinstance(this, (JSFunction, NativeFn)) and name in ("call", "apply"):
            if name == "call":
                return this.call(args[0] if args else UNDEF, list(args[1:]))
            return this.call(args[0] if args else UNDEF, list(args[1]) if len(args) > 1 and isinstance(args[1], list) else [])
        fn = self.get_member(this, name, node)
        return self.call_value(fn, this, args, node.pos)

    def get_member(self, obj, name, node):
        if isinstance(obj, HostObject):
            value = obj.js_get(name if isinstance(name, str) else to_str(name))
            return value
        if isinstance(obj, dict):
            if name in obj:
                return obj[name]
            if isinstance(name, int) and str(name) in obj:
                return obj[str(name)]
            return UNDEF
        if isinstance(obj, list):
            if name == "length":
                return float(len(obj))
            if isinstance(name, int):
                return obj[name] if 0 <= name < len(obj) else UNDEF
            if isinstance(name, str) and name in _ARRAY_METHODS:
                return NativeFn(lambda this, args, _n=name: _ARRAY_METHODS[_n](this, args), name)
            return UNDEF
        if isinstance(obj, str):
            if name == "length":
                return float(len(obj))
            if isinstance(name, int):
                return obj[name] if 0 <= name < len(obj) else UNDEF
            if name in _STRING_METHODS:
                return NativeFn(lambda this, args, _n=name: _STRING_METHODS[_n](obj, args), name)
            return UNDEF
        if isinstance(obj, (JSFunction, NativeFn)):
            if name == "prototype" and isinstance(obj, JSFunction):
                return obj.props.setdefault("prototype", {})
            return obj.props.get(name, UNDEF) if isinstance(obj, JSFunction) else UNDEF
        if obj is UNDEF or obj is None:
            raise Unsupported("property %s of %s" % (name, to_str(obj)), getattr(node, "pos", None))
        if hasattr(obj, "js_get"):
            return obj.js_get(name)
        return UNDEF

    def set_member(self, obj, name, value, node):
        if isinstance(obj, HostObject):
            obj.js_set(name if isinstance(name, str) else to_str(name), value)
        elif isinstance(obj, dict):
            obj[name] = value
        elif isinstance(obj, list):
            if isinstance(name, int):
                while len(obj) <= name:
                    obj.append(UNDEF)
                obj[name] = value
            elif name == "length":
                del obj[int(to_num(value)):]
            else:
                raise Unsupported("array property %s" % name, node.pos)
        elif isinstance(obj, JSFunction):
            obj.props[name] = value
        else:
            raise Unsupported("assignment to property %s of %s" % (name, to_str(obj)), node.pos)
