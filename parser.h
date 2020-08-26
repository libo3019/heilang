//
// Created by Alex on 8/23/2020.
//

#ifndef TINYLALR_PARSER_H
#define TINYLALR_PARSER_H
#include "json.hpp"
#include <iostream>
struct ParserSymbol {
    int type;
    int symbol;
    const char *text;
};
struct LexerState;
struct LexerTransition {
    int begin;
    int end;
    LexerState *state;
};
struct LexerState {
    LexerTransition *transitions;
    int transition_count;
    int symbol;
    inline LexerTransition *begin() { return transitions; }
    inline LexerTransition *end() { return transitions + transition_count; }
};
struct ReduceAction {
    const char *field;
    const char *desc;
    int type;
    int value;
};
struct ParserState;
struct ParserTransition {
    int type;
    int symbol;
    ParserState *state;
    int reduce_symbol;
    int reduce_length;
    ReduceAction *actions;
    int action_count;
};
struct ParserState {
    ParserTransition *transitions;
    int transition_count;
    inline ParserTransition *begin() { return transitions; }
    inline ParserTransition *end() { return transitions + transition_count; }
};

extern int LexerWhitespaceSymbol;
extern ParserSymbol ParserSymbols[];
extern LexerState LexerStates[];
extern LexerTransition LexerTransitions[];
extern ParserState ParserStates[];
extern ReduceAction ParserActions[];
extern ParserTransition ParserTransitions[];

using Value = nlohmann::json;
template <class char_t = char, class char_traits = std::char_traits<char_t>>
struct ParserNode {
    ParserState *state;
    int symbol;
    int line = 0;
    int column = 0;
    std::basic_string<char_t, char_traits> lexeme;
    Value value;
    ParserNode(ParserState *state, int symbol = 0) : state(state), symbol(symbol) {}
    ParserNode(ParserState *state, int symbol, int line, int column,
               const std::basic_string<char_t, char_traits> &lexeme, const Value &value) :
            state(state), symbol(symbol),
            line(line), column(column),
            lexeme(lexeme),
            value(std::move(value)) {}
};
template <class iter_t = const char *, class char_t = typename std::iterator_traits<iter_t>::value_type, class char_traits = std::char_traits<char_t>>
class ParserLexer {
    using uchar_t = typename std::make_unsigned<char_t>::type;
    using string_t = std::basic_string<char_t, char_traits>;
    LexerState *lexer_state /* = &LexerStates[0]*/;
    int whitespace /*= LexerWhitespaceSymbol*/;
    iter_t current;
    iter_t end;
    int line_start = 0;
    int token_start = 0;
    int token_symbol = 0;
    int line_ = 0;
    int position_ = 0;
    string_t lexeme_;
private:
    inline LexerTransition *find_trans(LexerState *state, uchar_t chr) {
        LexerTransition *dot_trans = nullptr;
        for (auto &trans : *state) {
            if (trans.end == -1) {
                dot_trans = &trans;
            }
            if (trans.begin <= chr && chr <= trans.end) {
                return &trans;
            }
        }
        return dot_trans;
    }
    auto advance_symbol() {
        LexerState *state = lexer_state;
        lexeme_.clear();
        do {
            if (current == end) {
                return state->symbol;
            }
            auto *trans = find_trans(state, *current);
            if (trans) {
                lexeme_ += *current;
                state = trans->state;
                ++position_;
                if (*current == char_t('\n')) {
                    ++line_;
                    line_start = position_;
                }
                ++current;
            } else {
                break;
            }
        } while (true);
        if (state == lexer_state && *current != '\0') {
            std::cout << "Unexpect char: " << *current << " line:" << line() << std::endl;
            ++current;
            return 2; // error symbol
        }
        return state->symbol;
    }
public:
    ParserLexer(LexerState *state, int whitespace = -1) : lexer_state(state), whitespace(whitespace) {}
    void reset(iter_t first, iter_t last) {
        current = first;
        end = last;
    }
    void advance() {
        do {
            token_start = position_;
            token_symbol = advance_symbol();
        } while (token_symbol == whitespace);
    }
    int symbol() { return token_symbol; }
    int line() const { return line_; }
    int column() const { return token_start - line_start; }
    string_t &lexeme() { return lexeme_; }
    void dump() {
        do {
            advance();
            std::cout << lexeme_ << "  " << token_symbol
                      << "[" << line() << ", " << column() << "]" << std::endl;

            if (token_symbol == 0) {
                break;
            }
        } while (symbol() != 0);
        exit(0);
    }
};

template <class iter_t = const char *, class char_t = typename std::iterator_traits<iter_t>::value_type, class char_traits = std::char_traits<char_t>>
class Parser {
    using Lexer = ParserLexer<iter_t>;
    using Node = ParserNode<char_t, char_traits>;
    ParserState *parser_state = &ParserStates[0];
    Lexer parser_lexer = Lexer(&LexerStates[0], LexerWhitespaceSymbol);
    bool position = false;
    ParserTransition *find_trans(ParserState *state, int symbol) {
        for (auto &trans : *state) {
            if (trans.symbol == symbol) {
                return &trans;
            }
        }
        return nullptr;
    }
public:
    std::vector<Node> stack;
    Parser() = default;
    void set_position(bool sp) {
        position = sp;
    }
    void reset(iter_t first, iter_t last = iter_t()) {
        parser_lexer.reset(first, last);
    }
    void parse() {
        parser_lexer.advance();
        stack.reserve(32);
        stack.push_back(Node(parser_state));
        do {
            auto *trans = find_trans(stack.back().state, parser_lexer.symbol());
            if (!trans) {
                expect();
                break;
            }
            if (trans->type == 1) { //Shift
                //debug_shift(trans);
                stack.emplace_back(trans->state, parser_lexer.symbol(), parser_lexer.line(), parser_lexer.column(),
                                   parser_lexer.lexeme(), Value());
                parser_lexer.advance();
            } else {
                reduce(trans);
                if (stack.back().symbol == 0) {
                    break;
                }
            }
        } while (true);
    }
    Value &value() { return stack[0].value; }
    inline void reduce(ParserTransition *trans) {
        auto stack_start = (stack.size() - trans->reduce_length);
        //debug_reduce(trans, stack_start);
        Value reduce_value;
        int line = 0;
        int column = 0;
        if (stack_start != stack.size()) {
            line = stack[stack_start].line;
            column = stack[stack_start].column;
            reduce_value = handle(trans->actions, trans->action_count, &stack[stack_start]);
            if (position && reduce_value.is_object()) {
                reduce_value["position"] = {{"line",   line},
                                            {"column", column}};
            }
            stack.erase(stack.begin() + stack_start, stack.end());
            if (trans->reduce_symbol == 0) {
                stack.back().value = std::move(reduce_value);
                return;
            }
        }
        auto *new_trans = find_trans(stack.back().state, trans->reduce_symbol);
        stack.emplace_back(new_trans->state,
                           trans->reduce_symbol,
                           line, column,
                           std::string(ParserSymbols[trans->reduce_symbol].text),
                           reduce_value);

    }
    inline void debug_shift(ParserTransition *trans) {
        std::cout << "shift: " << stack.size() << " "
                  << "[state " << (stack.back().state - ParserStates) << " -> " << (trans->state - ParserStates)
                  << "]  [" << ParserSymbols[parser_lexer.symbol()].text << "] "
                  << parser_lexer.lexeme()
                  << std::endl << std::endl;
    }
    inline void debug_reduce(ParserTransition *trans, int start) {
        ParserTransition *goto_trans = find_trans(stack[start - 1].state, trans->reduce_symbol);
        std::cout << "reduce: "
                  << start << " "
                  << "[back to " /*<< (stack.back().state - ParserStates) << " -> "*/
                  << (stack[start - 1].state - ParserStates) << " -> "
                  << (goto_trans->state - ParserStates) << "] "
                  << ParserSymbols[trans->reduce_symbol].text << " <- ";
        for (int i = start; i < start + trans->reduce_length; ++i) {
            std::cout << "[" << stack[i].lexeme << "] " << stack[i].value << " | ";
        }
        std::cout << std::endl << std::endl;
    }
    inline void expect() {
        Node &node = stack.back();
        std::cout << "Shift Reduce Error "
                     "line: " << node.line + 1 << " "
                  << "column: " << node.column + 1 << " "
                  << "token: " << parser_lexer.lexeme()
                  << std::endl;
        std::cout << "Expect: ";
        for (auto &trans : *node.state) {
            std::cout << "\"" << ParserSymbols[trans.symbol].text << "\"";
            if (&trans != (node.state->end() - 1)) {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;

    }
    Value handle(ReduceAction *actions, int action_count, Node *nodes) {
        if (action_count == 0) {
            return std::move(nodes->value);
        }
        Value value;
        for (int i = 0; i < action_count; ++i) {
            auto &action = actions[i];
            if (action.type == 0) {
                value = std::move((nodes + action.value)->value);
            }
            if (action.type == 1) {
                value = (nodes + action.value)->lexeme.c_str();
            }
            if (action.type == 2) {
                // $n set value
                auto &field = value[std::string(action.field)];
                if (field.empty()) {
                    field = std::move((nodes + action.value)->value);
                } else if (field.is_array()) {
                    field.emplace_back(std::move((nodes + action.value)->value));
                } else {
                    field = Value::array({std::move(field), std::move((nodes + action.value)->value)});
                }
            }
            if (action.type == 3) {
                // @n set lexeme
                value[std::string(action.field)] = (nodes + action.value)->lexeme.c_str();
            }
            if (action.type == 4) {
                // #n insert
                auto &field = value[std::string(action.field)];
                if (field.empty()) {
                    field = Value::array();
                }
                if (field.is_array()) {
                    field.emplace_back(std::move((nodes + action.value)->value));
                } else {
                    field = Value::array({std::move(field), std::move((nodes + action.value)->value)});
                }

            }
            if (action.type == 5) {
                value[std::string(action.field)] = action.desc;
            }
            if (action.type == 6) {
                value[std::string(action.field)] = Value::boolean_t(action.value);
            }
            if (action.type == 7) {
                value[std::string(action.field)] = Value::number_integer_t(action.value);
            }
        }
        return std::move(value);
    }
};

#endif //TINYLALR_PARSER_H