let FUNCTIONS = {
  sqrt: Math.sqrt,
  sin: Math.sin,
  cos: Math.cos,
  tan: Math.tan,
  log: Math.log10,
  ln: Math.log,
  abs: Math.abs,
  round: Math.round,
  floor: Math.floor,
  ceil: Math.ceil,
};

let CONSTANTS = { pi: Math.PI, e: Math.E, tau: Math.PI * 2 };

export function calculate(input) {
  let tokens = input.toLowerCase().match(/\d*\.?\d+(?:e[+-]?\d+)?|[a-z]+|[-+*/%^()]|\S/g) ?? [];
  let pos = 0;

  function peek() {
    return tokens[pos];
  }

  function expr() {
    let value = term();
    while (peek() == "+" || peek() == "-") value = tokens[pos++] == "+" ? value + term() : value - term();
    return value;
  }

  function term() {
    let value = unary();
    while (peek() == "*" || peek() == "/" || peek() == "%" || peek() == "x") {
      let op = tokens[pos++];
      let right = unary();
      value = op == "/" ? value / right : op == "%" ? value % right : value * right;
    }
    return value;
  }

  function unary() {
    if (peek() == "-") {
      pos++;
      return -unary();
    }
    if (peek() == "+") {
      pos++;
      return unary();
    }
    return power();
  }

  function power() {
    let base = atom();
    if (peek() == "^") {
      pos++;
      return base ** unary();
    }
    return base;
  }

  function atom() {
    let token = tokens[pos++];
    if (token == "(") {
      let value = expr();
      if (tokens[pos++] != ")") throw new Error("paren");
      return value;
    }
    if (token in FUNCTIONS) {
      if (tokens[pos++] != "(") throw new Error("call");
      let value = expr();
      if (tokens[pos++] != ")") throw new Error("paren");
      return FUNCTIONS[token](value);
    }
    if (token in CONSTANTS) return CONSTANTS[token];
    let value = Number(token);
    if (token == null || isNaN(value)) throw new Error("token");
    return value;
  }

  let value = expr();
  if (pos != tokens.length || !isFinite(value)) throw new Error("syntax");
  return String(Number(value.toPrecision(12)));
}
