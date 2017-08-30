
#include <string>
#include <vector>

#include "lex.h"

namespace hit
{

const std::string digits = "0123456789";
const std::string alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
const std::string space = " \t";
const std::string allspace = " \t\n\r";
const std::string newline = "\n\r";
const std::string alphanumeric = digits + alpha;
const std::string identchars = alphanumeric + "_./:<>-+";

_LexFunc::_LexFunc(LexFunc pp) : p(pp) {}
_LexFunc::operator LexFunc() { return p; }

bool
charIn(char c, const std::string & valid)
{
  return valid.find(c) != std::string::npos;
}

// the EOF macro wreaks havok on our TokType enum which has an EOF member. So undefine it and the
// redefine it at the end of the file.
#define TMPEOF EOF
#undef EOF

int
lineNum(size_t offset, const std::string & input)
{
  int line = 0;
  size_t pos = input.find("\n", 0); // fist occurrence
  while (pos < offset)
  {
    line++;
    pos = input.find("\n", pos + 1);
  }
  return line + 1;
}

std::string
tokTypeName(TokType t)
{
// clang-format off
  #define tokcase(type) case TokType::type: return #type;
  switch (t)
    {
      tokcase(Error)
      tokcase(Equals)
      tokcase(LeftBracket)
      tokcase(RightBracket)
      tokcase(Ident)
      tokcase(Path)
      tokcase(Number)
      tokcase(String)
      tokcase(Comment)
      tokcase(InlineComment)
      tokcase(EOF)
      default : return std::to_string((int)t);
    }
  #undef tokcase
  // clang-format on
}

Token::Token(TokType t, const std::string & val, size_t offset, int line)
  : type(t), val(val), offset(offset), line(line)
{
}

std::string
Token::str()
{
  if (type == TokType::String || type == TokType::Error)
    return tokTypeName(type) + ":" + val;
  return tokTypeName(type) + ":'" + val + "'";
}

Lexer::Lexer(const std::string & name, const std::string & input) : _name(name), _input(input) {}
std::vector<Token>
Lexer::tokens()
{
  return _tokens;
}

std::vector<Token>
Lexer::run(LexFunc start)
{
  LexFunc state = start;
  while (state != nullptr)
    state = state(this);
  return _tokens;
}

void
Lexer::emit(TokType type)
{
  _tokens.push_back(
      Token(type, _input.substr(_start, _pos - _start), _start, lineNum(_start, _input)));
  _start = _pos;
}

LexFunc
Lexer::error(const std::string & msg)
{
  _tokens.push_back(Token(TokType::Error, msg, _start, lineNum(_start, _input)));
  return nullptr;
}

char
Lexer::next()
{
  if (_pos >= _input.size())
  {
    _width = 0;
    return 0;
  }

  char c = _input[_pos];
  _width = 1;
  _pos += _width;
  return c;
}

bool
Lexer::accept(const std::string & valid)
{
  if (charIn(next(), valid))
    return true;
  backup();
  return false;
}

int
Lexer::acceptRun(const std::string & valid)
{
  int n = 0;
  while (true)
  {
    size_t index = valid.find(next());
    if (index == std::string::npos)
      break;
    n++;
  }
  backup();
  return n;
}

char
Lexer::peek()
{
  char c = next();
  backup();
  return c;
}

void
Lexer::ignore()
{
  _start = _pos;
}
void
Lexer::backup()
{
  _pos -= _width;
}

std::string
Lexer::input()
{
  return _input;
}
size_t
Lexer::start()
{
  return _start;
}
size_t
Lexer::pos()
{
  return _pos;
}

_LexFunc lexHit(Lexer *);
_LexFunc lexNumber(Lexer *);
_LexFunc lexString(Lexer *);

_LexFunc
lexPath(Lexer * l)
{
  l->acceptRun(space);
  l->ignore();
  l->acceptRun(identchars);
  l->emit(TokType::Path);

  l->acceptRun(space);
  l->ignore();
  if (!l->accept("]"))
    return l->error("invalid section path character '" + std::string(1, l->peek()) + "'");

  l->emit(TokType::RightBracket);
  return lexHit;
}

void
consumeToNewline(Lexer * l)
{
  while (true)
  {
    char c = l->next();
    if (c == '\0' || charIn(c, "\n\r"))
      break;
  }
  l->backup();
}

void
lexComments(Lexer * l)
{
  l->acceptRun(space);
  l->ignore();
  if (l->accept("#"))
  {
    consumeToNewline(l);
    l->emit(TokType::InlineComment);
  }

  while (true)
  {
    l->acceptRun(allspace);
    l->ignore();
    if (!l->accept("#"))
      break;
    consumeToNewline(l);
    l->emit(TokType::Comment);
  }
}

_LexFunc
lexEq(Lexer * l)
{
  l->acceptRun(space);
  l->ignore();
  if (!l->accept("="))
    return l->error("expected '=' after parameter name '" + l->tokens().back().val + "', got '" +
                    std::string(1, l->next()) + "'");
  l->emit(TokType::Equals);

  l->acceptRun(allspace);
  l->ignore();

  // uncomment this to allow commentw between '=' and field value
  // lexComments(l);
  // l->acceptRun(allspace);
  // l->ignore();

  if (charIn(l->peek(), digits + "-+."))
    return lexNumber;
  return lexString;
}

void
consumeUnquotedString(Lexer * l)
{
  // check for dollar substitution syntax
  if (l->peek() == '$')
  {
    while (true)
    {
      char c = l->next();
      // '#' is always a comment outside of quoted string
      if (c == '\0' || charIn(c, newline + "#"))
        break;
    }
    l->backup();
    return;
  }

  while (true)
  {
    char c = l->next();
    // '#' is always a comment outside of quoted string
    if (c == '\0' || charIn(c, allspace + "[#"))
      break;
  }
  l->backup();
}

_LexFunc
lexString(Lexer * l)
{
  l->acceptRun(allspace);
  l->ignore();

  if (!charIn(l->peek(), "'\""))
    consumeUnquotedString(l);
  else
  {
    char quote = 0;
    if (l->accept("\""))
      quote = '"';
    else if (l->accept("'"))
      quote = '\'';

    char c = l->input()[l->start()];
    char prev = c;
    while (true)
    {
      prev = c;
      c = l->next();
      if (c == quote and prev != '\\')
        break;
      else if (c == '\0')
        return l->error("unterminated string");
    }
  }
  l->emit(TokType::String);
  return lexHit;
}

_LexFunc
lexNumber(Lexer * l)
{
  l->accept("+-");
  int n = l->acceptRun(digits);
  if (l->accept("."))
    n += l->acceptRun(digits);

  if (n == 0)
  {
    // fall back to string
    consumeUnquotedString(l);
    l->emit(TokType::String);
    return lexHit;
  }

  if (l->accept("eE"))
  {
    l->accept("-+");
    l->acceptRun(digits);
  }

  if (!charIn(l->peek(), allspace))
  {
    // fall back to string
    consumeUnquotedString(l);
    l->emit(TokType::String);
    return lexHit;
  }

  l->emit(TokType::Number);
  return lexHit;
}

_LexFunc
lexHit(Lexer * l)
{
  lexComments(l);
  l->acceptRun(allspace);
  l->ignore();
  char c = l->next();
  if (c == '[')
  {
    l->emit(TokType::LeftBracket);
    return lexPath;
  }
  else if (charIn(c, identchars))
  {
    l->acceptRun(identchars);
    l->emit(TokType::Ident);
    return lexEq;
  }
  else if (c == '\0')
  {
    l->emit(TokType::EOF);
    return NULL;
  }
  return l->error("invalid character '" + std::string(1, c) + "'");
}

} // namespace hit

#define EOF TMPEOF
