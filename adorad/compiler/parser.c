/*
          _____   ____  _____            _____
    /\   |  __ \ / __ \|  __ \     /\   |  __ \
   /  \  | |  | | |  | | |__) |   /  \  | |  | | Adorad - The Fast, Expressive & Elegant Programming Language
  / /\ \ | |  | | |  | |  _  /   / /\ \ | |  | | Languages: C, C++, and Assembly
 / ____ \| |__| | |__| | | \ \  / ____ \| |__| | https://github.com/adorad/adorad/
/_/    \_\_____/ \____/|_|  \_\/_/    \_\_____/

Licensed under the MIT License <http://opensource.org/licenses/MIT>
SPDX-License-Identifier: MIT
Copyright (c) 2021 Jason Dsouza <@jasmcaus>
*/

#include <stdlib.h>
#include <adorad/compiler/parser.h>
#include <adorad/core/debug.h>
#include <adorad/core/vector.h>

// Shortcut to `parser->toklist`
#define pt  parser->toklist
#define ast_error(...)  panic(ErrorParseError, __VA_ARGS__)

// Initialize a new Parser
Parser* parser_init(Lexer* lexer) {
    Parser* parser = cast(Parser*)calloc(1, sizeof(Parser));
    parser->lexer = lexer;
    parser->toklist = lexer->toklist;
    parser->curr_tok = cast(Token*)vec_at(parser->toklist, 0);
    parser->num_tokens = vec_size(parser->toklist);
    parser->num_lines = 0;
    parser->mod_name = null;
    parser->defer_vars = null;
    return parser;
}

inline Token* parser_peek_token(Parser* parser) {
    return parser->curr_tok;
}

// Consumes a token and moves on to the next token
inline Token* parser_chomp(Parser* parser) {
    Token* tok = parser_peek_token(parser);
    parser->curr_tok += 1;
    return tok;
}

// Consumes a token and moves on to the next, if the current token matches the expected token.
inline Token* parser_chomp_if(Parser* parser, TokenKind tokenkind) {
    if(parser->curr_tok->kind == tokenkind)
        return parser_chomp(parser);

    return null;
}

inline Token* parser_expect_token(Parser* parser, TokenKind tokenkind) {
    if(parser->curr_tok->kind == tokenkind)
        return parser_chomp(parser);
        
    panic(ErrorUnexpectedToken, "Expected `%s`; got `%s`", 
                                        token_to_buff(tokenkind)->data,
                                        token_to_buff(parser->curr_tok->kind)->data);
    abort();
}

AstNode* ast_create_node(AstNodeKind kind) {
    AstNode* node = cast(AstNode*)calloc(1, sizeof(AstNode));
    node->kind = kind;
    return node;
}

AstNode* ast_clone_node(AstNode* node) {
    if(!node)
        panic(ErrorUnexpectedNull, "Trying to clone a null AstNode?");
    AstNode* new = ast_create_node(node->kind);
    // TODO(jasmcaus): Add more struct members
    return new;
}

/*
    A large part of the Parser from this point onwards has been selfishly stolen from Zig's Compiler.

    Before, we release the first stable version of Adorad, this parser implementation will be reworked and improved.

    Related source code: https://github.com/ziglang/zig/blob/master/src/stage1/parser.cpp
*/

// General format:
//      KEYWORD(func) IDENT LPAREN ParamDeclList RPAREN LARROW RETURNTYPE
static AstNode* ast_parse_func_prototype(Parser* parser) {
    Token* func = parser_chomp_if(parser, FUNC);
    if(func == null)
        return null;
    
    Token* identifier = parser_chomp_if(parser, IDENTIFIER);
    parser_expect_token(parser, LPAREN);
    Vec* params = ast_parse_list(params, COMM, ast_parse_match_prong;
    parser_expect_token(params, RPAREN);

    AstNode* return_type = ast_parse_type_expr(parser);
    if(return_type == null) {
        Token* next = parser_peek_token(parser);
        ast_error(
            "expected return type; found`%s`",
            token_to_buff(next->kind)->data
        );
    }

    AstNode* out = ast_create_node(AstNodeKindFuncPrototype);
    out->data.stmt->func_proto_decl;
    out->data.stmt->func_proto_decl->name = identifier->value;
    out->data.stmt->func_proto_decl->params = params;
    out->data.stmt->func_proto_decl->return_type = return_type;

    for(UInt64 i = 0; i < vec_size(params); i++) {
        AstNode* param_decl = vec_at(params, i);
        CORETEN_CHECK(param_decl->kind == AstNodeKindParamDecl);
        if(param_decl->data.param_decl->is_var_args);
            out->data.stmt->func_proto_decl->is_var_args = true;
        
        // Check for multiple variadic arguments in prototype
        // Adorad supports only 1
        if(i != vec_size(params) - 1 && out->data.stmt->func_proto_decl->is_var_args)
            ast_error(
                "Cannot have multiple variadic arguments in function prototype"
            );
    }
    return out;
}

// General format:
// `?` represents optional
//      KEYWORD(export)? KEYWORD(mutable/const)? TypeExpr? IDENTIFIER EQUAL? Expr?
static AstNode* ast_parse_var_decl(Parser* parser) {
    Token* export_kwd = parser_chomp_if(parser, EXPORT);
    Token* mutable_kwd = parser_chomp_if(parser, MUTABLE);
    Token* const_kwd = parser_chomp_if(parser, CONST);
    if(mutable_kwd && const_kwd)
        ast_error("Cannot decorate a variable as both `mutable` and `const`");

    AstNode* type_expr = ast_parse_type_expr(parser);
    Token* identifier = parser_expect_token(parser, IDENTIFIER);
    Token* equals = parser_chomp_if(parser, EQUALS);
    AstNode* expr;
    if(equals != null)
        expr = ast_parse_expr(parser);
    
    parser_expect_token(parser, SEMICOLON); // TODO: Remove this need

    AstNode* out = ast_create_node(AstNodeKindVarDecl);
    out->data.stmt->var_decl->name = identifier->value;
    out->data.stmt->var_decl->is_export = export_kwd != null;
    out->data.stmt->var_decl->is_mutable = mutable_kwd != null;
    out->data.stmt->var_decl->is_const = const_kwd != null;
    out->data.stmt->var_decl->expr = expr;
    return out;
}

// Statements
static AstNode* ast_parse_statement(Parser* parser) {
    AstNode* var_decl = ast_parse_var_decl(parser);
    if(var_decl != null) {
        CORETEN_CHECK(var_decl->kind == AstNodeKindVarDecl);
        return var_decl;
    }
    free(var_decl);

    // Defer
    Token* defer_stmt = parser_chomp_if(parser, DEFER);
    if(defer_stmt != null) {
        AstNode* statement = ast_parse_block_expr_statement(parser);
        AstNode* out = ast_create_node(AstNodeKindDefer);
        
        out->data.stmt->defer_stmt->expr = statement;
        return out;
    }
    free(defer_stmt);

    // If statement
    AstNode* if_statement = ast_parse_if_statement(parser);
    if(if_statement != null)
        return if_statement;
    free(if_statement);
    
    // Labeled Statements
    AstNode* labeled_statement = ast_parse_labeled_statements(parser);
    if(labeled_statement != null)
        return labeled_statement;
    free(labeled_statement);

    // Match statements
    AstNode* match_expr = ast_parse_match_expr(parser);
    if(match_expr != null)
        return match_expr;
    free(match_expr);

    // Assignment statements
    AstNode* assignment_expr = ast_parse_assignment_expr(parser);
    if(assignment_expr != null)
        return assignment_expr;
    free(assignment_expr);

    return null;
}

static AstNode* ast_parse_if_prefix(Parser* parser) {
    Token* if_kwd = parser_chomp_if(parser, IF);
    if(if_kwd == null) {
        free(if_kwd);
        return null;
    }
    Token* lparen = parser_expect_token(parser, LPAREN);
    AstNode* condition = ast_parse_expr(parser);
    Token* rparen = parser_expect_token(parser, RPAREN);
    free(lparen);
    free(rparen);

    AstNode* out = ast_clone_node(AstNodeKindIfExpr);
    out->data.expr->if_expr->condition = condition;

    return out;
}

static AstNode* ast_parse_if_statement(Parser* parser) {
    AstNode* out = ast_parse_if_prefix(parser);
    if(out == null) {
        free(out);
        return null;
    }
    
    AstNode* body = ast_parse_block_expr(parser);
    if(body == null)
        body = ast_parse_assignment_expr(parser);
    
    if(body == null) {
        Token* token = parser_chomp(parser);
        ast_error(
            "expected `if` body; found `%s`",
            token_to_buff(token->kind)->data
        );
    }

    AstNode* else_body = null;
    AstNode* else_kwd = parser_chomp_if(parser, ELSE);
    if(else_kwd != null)
        else_body = ast_parse_statement(parser);
    free(else_kwd);

    out->data.expr->if_expr->then_block = body;
    out->data.expr->if_expr->has_else = else_body != null;
    out->data.expr->if_expr->else_node = else_body;
    return out;
}

// Labeled Statements
static AstNode* ast_parse_labeled_statements(Parser* parser) {
    Token* label = ast_parse_block_label(parser);
    AstNode* block = ast_parse_block(parser);
    if(block != null) {
        CORETEN_CHECK(block->kind == AstNodeKindBlock);
        block->data.stmt->block_stmt->name = label->value;
        return block;
    }
    free(block);

    AstNode* loop = ast_parse_loop_statement(parser);
    if(loop != null) {
        loop->data.expr->loop_expr->label = label->value;
        return loop;
    }

    if(label != null)
        panic(
            ErrorUnexpectedToken,
            "invalid token: `%s`",
            parser_peek_token(parser)->value
        );
        
    return null;
}

// Loops
//      (KEYWORD(inline))? loop ... {  }
static AstNode* ast_parse_loop_statement(Parser* parser) {
    Token* inline_token = parser_chomp_if(parser, INLINE);

    AstNode* loop_c_statement = ast_parse_loop_c_statement(parser);
    if(loop_c_statement != null) {
        CORETEN_CHECK(loop_c_statement->kind == AstNodeKindLoopCExpr);
        loop_c_statement->data.expr->loop_expr->loop_c_expr->is_inline = inline_token != null;
        free(inline_token);
        return loop_c_statement;
    }

    AstNode* loop_while_statement = ast_parse_loop_while_statement(parser);
    if(loop_while_statement != null) {
        CORETEN_CHECK(loop_while_statement->kind == AstNodeKindLoopWhileExpr);
        loop_while_statement->data.expr->loop_expr->loop_while_expr->is_inline = inline_token != null;
        free(inline_token);
        return loop_while_statement;
    }

    AstNode* loop_in_statement = ast_parse_loop_in_statement(parser);
    if(loop_in_statement != null) {
        CORETEN_CHECK(loop_in_statement->kind == AstNodeKindLoopWhileExpr);
        loop_in_statement->data.expr->loop_expr->loop_in_expr->is_inline = inline_token != null;
        free(inline_token);
        return loop_in_statement;
    }

    if(inline_token != null)
        panic(
            ErrorUnexpectedToken,
            "invalid token: `%s`",
            parser_peek_token(parser)->value
        );
}

// Block Statement
//     BlockExpr       // { ... }
//     AssignmentExpr 
static AstNode* ast_parse_block_expr_statement(Parser* parser) {
    AstNode* block = ast_parse_block_expr(parser);
    if(block != null)
        return block;
    
    AstNode* assignment_expr = ast_parse_assignment_expr(parser);
    if(assignment_expr != null) {
        Token* semi = parser_expect_token(parser, SEMICOLON);
        free(semi);
        return assignment_expr;
    }
    
    return null;
}

// Block Expression
//      (BlockLabel)? block
static AstNode* ast_parse_block_expr(Parser* parser) {
    Token* block_label = ast_parse_block_label(parser);
    if(block_label != null) {
        AstNode* out = ast_parse_block(parser);
        CORETEN_CHECK(out->kind == AstNodeKindBlock);
        out->data.stmt->block_stmt->name = block_label->value;
        return out;
    }

    return ast_parse_block(parser);
}

// AssignmentExpr
//      Expr (AssignmentOp Expr)?
// AssignmentOp can be one of:
//      | MULT_EQUALS       (*=)
//      | SLASH_EQUALS      (/=)
//      | MOD_EQUALS        (%=)
//      | PLUS_EQUALS       (+=)
//      | MINUS_EQUALS      (-=)
//      | LBITSHIFT_EQUALS  (<<=)
//      | RBITSHIFT_EQUALS  (>>=)
//      | AND_EQUALS        (&=)
//      | XOR_EQUALS        (^=)
//      | OR_EQUALS         (|=)
//      | TILDA             (~)
//      | TILDA_EQUALS      (~=)
//      | EQUALS            (=)
static AstNode* ast_parse_assignment_expr(Parser* parser) {
    return ast_parse_binary_op_expr(parser);
}

static AstNode* ast_parse_block(Parser* parser) {
    Token* lbrace = parser_chomp_if(parser, LBRACE);
    if(lbrace == null)
        return null;

    Vec* statements = vec_new(AstNode, 1);
    AstNode* statement = null;
    while((statement = ast_parse_statement(parser)) != null)
        vec_push(statements, statement);

    Token* rbrace = parser_expect_token(parser, RBRACE);
    free(lbrace);
    free(rbrace);

    AstNode* out = ast_create_node(AstNodeKindBlock);
    out->data.stmt->block_stmt->statements = statements;
    return out;
}

typedef struct ast_prec_table {
    TokenKind tok_kind;
    UInt8 prec_value;
    BinaryOpKind bin_kind;
} ast_prec_table;

// A table of binary operator precedence. Higher precedence numbers are stickier.
static const ast_prec_table precedence_table[] = {
    // { MULT_MULT, 60, BinaryOpKindMultMult  },
    { MULT, 60, BinaryOpKindMult  },
    { MOD, 60, BinaryOpKindMod  },
    { SLASH, 60, BinaryOpKindDiv  },

    { PLUS, 50, BinaryOpKindAdd  },
    { MINUS, 50, BinaryOpKindSubtract  },
    { PLUS_EQUALS, 50, BinaryOpKindAssignmentPlus  },
    { MINUS_EQUALS, 50, BinaryOpKindAssignmentMinus  },

    { LBITSHIFT, 40, BinaryOpKindBitshitLeft  },
    { RBITSHIFT, 40, BinaryOpKindBitshitRight  },
    
    { EQUALS_EQUALS, 30, BinaryOpKindCmpEqual  },
    { EXCLAMATION_EQUALS, 30, BinaryOpKindCmpNotEqual  },
    { GREATER_THAN, 30, BinaryOpKindCmpGreaterThan  },
    { LESS_THAN, 30, BinaryOpKindCmpLessThan  },
    { GREATER_THAN_OR_EQUAL_TO, 30, BinaryOpKindCmpGreaterThanorEqualTo  },
    { LESS_THAN_OR_EQUAL_TO, 30, BinaryOpKindCmpLessThanorEqualTo  },

    { AND, 20, BinaryOpKindBoolAnd  },

    { OR, 10, BinaryOpKindBoolOr  },
};

// This has been selfishly ported from Zig's Compiler
// Source for this: https://github.com/ziglang/zig/blob/master/src/stage1/parser.cpp
typedef enum BinaryOpChain {
    BinaryOpChainOnce,
    BinaryOpChainInfinity
} BinaryOpChain;

// A `generic`-like function that parses binary expressions.
// These (expressions) utilize similar functionality, so this function is here to avoid code duplication.
// Here, `op_parser` parses the operand (e.g. `+=`, `or`...)
static AstNode* ast_parse_binary_op_expr(
    Parser* parser,
    BinaryOpChain chain,
    AstNode* (*op_parser)(Parser*),
    AstNode* (*child_parser)(Parser*)
) {
    AstNode* out = child_parser(parser);
    if(out == null)
        return null;

    do {
        AstNode* op = op_parser(parser);
        if(op == null)
            break;

        AstNode* left = out;
        AstNode* right = child_parser(parser);
        out = op;

        if(op->kind == AstNodeKindBinaryOpExpr) {
            op->data.expr->binary_op_expr->lhs = left;
            op->data.expr->binary_op_expr->rhs = right;
        } else {
            unreachable();
        }
    } while(chain == BinaryOpChainInfinity);

    return out;
}

// BooleanAndExpr
static AstNode* ast_parse_bool_and_expr(Parser* parser) {
    return ast_parse_binary_op_expr(
        parser,
        BinaryOpChainInfinity,
        ast_parse_boolean_and_op,
        ast_parse_bool_or_expr
    );
}

// BooleanOrExpr
static AstNode* ast_parse_bool_or_expr(Parser* parser) {
    return ast_parse_binary_op_expr(
        parser,
        BinaryOpChainInfinity,
        ast_parse_boolean_or_op,
        ast_parse_comparison_expr
    );
}

// ComparisonExpr
//      BitwiseExpr (ComparisonOp BitwiseExpr)*
static AstNode* ast_parse_comparison_expr(Parser* parser) {
    return ast_parse_binary_op_expr(
        parser,
        BinaryOpChainOnce,
        ast_parse_comparison_op,
        ast_parse_bitwise_expr
    ); 
}

// BitwiseExpr
//      BitShiftExpr (BitwiseOp BitShiftExpr)*
static AstNode* ast_parse_bitwise_expr(Parser* parser) {
    return ast_parse_binary_op_expr(
        parser,
        BinaryOpChainInfinity,
        ast_parse_bitwise_op,
        ast_parse_bitshift_expr
    ); 
}

// BitShiftExpr
//      AdditionExpr (BitshiftOp AdditionExpr)*
static AstNode* ast_parse_bitshift_expr(Parser* parser) {
    return ast_parse_binary_op_expr(
        parser,
        BinaryOpChainInfinity,
        ast_parse_bitshift_op,
        ast_parse_addition_expr
    ); 
}

// AdditionExpr
//      MultiplyExpr (AdditionOp MultiplyExpr)*
static AstNode* ast_parse_addition_expr(Parser* parser) {
    return ast_parse_binary_op_expr(
        parser,
        BinaryOpChainInfinity,
        ast_parse_addition_op,
        ast_parse_multiplication_expr
    ); 
}

// MultiplyExpr
//      AdditionExpr (MultiplicationOp AdditionExpr)*
static AstNode* ast_parse_multiplication_expr(Parser* parser) {
    return ast_parse_binary_op_expr(
        parser,
        BinaryOpChainInfinity,
        ast_parse_multiplication_op,
        ast_parse_bitwise_expr
    ); 
}

// PrefixExpr
//      PrefixOp? PrimaryExpr
// PrefixOp can be one of:
//      | EXCLAMATION   (!)
//      | MINUS         (-)
//      | TILDA         (~)
//      | AND           (&)
//      | KEYWORD(try) 
static AstNode* ast_parse_prefix_expr(Parser* parser) {
    return ast_parse_prefix_op_expr(
        parser,
        ast_parse_prefix_op,
        ast_parse_primary_expr
    );
}

// PrimaryExpr
//      | IfExpr
//      | KEYWORD(break) BreakLabel? Expr?
//      | KEYWORD(continue) BreakLabel?
//      | ATTRIBUTE Expr
//      | KEYWORD(return) Expr?
//      | BlockLabel? LoopExpr
//      | Block
static AstNode* ast_parse_primary_expr(Parser* parser) {
    AstNode* if_expr = ast_parse_if_expr(pc);
    if (if_expr != null)
        return if_expr;

    Token* break_token = parser_chomp_if(parser, BREAK);
    if(break_token != null) {
        free(break_token);
        Token* label = ast_parse_break_label(parser);
        AstNode* expr = ast_parse_expr(parser);
        
        AstNode* out = ast_create_node(AstNodeKindBreak);
        out->data.stmt->branch_stmt->name = label->value;
        out->data.stmt->branch_stmt->type = AstNodeBranchStatementBreak;
        out->data.stmt->branch_stmt->expr = expr;
        return out;
    }
    
    Token* continue_token = parser_chomp_if(parser, CONTINUE);
    if(continue_token != null) {
        Token* label = ast_parse_break_label(parser);
        AstNode* out = ast_create_node(AstNodeKindContinue);
        out->data.stmt->branch_stmt->name = label == null ? label->value : null;
        out->data.stmt->branch_stmt->type = AstNodeBranchStatementContinue;
    }

    // Token* attribute = parser_chomp_if(parser, ATTRIBUTE);
    // if (attribute != 0) {
    //     AstNode* expr = ast_parse_expr();
    //     AstNode* out = ast_create_node(AstNodeKindAttribute);
    //     out->data.attribute_expr.expr = expr;
    //     return out;
    // }

    Token* return_token = parser_chomp_if(parser, RETURN);
    if(return_token != null) {
        free(return_token);
        AstNode* expr = ast_parse_expr(parser);
        AstNode* out = ast_create_node(AstNodeKindReturn);
        out->data.stmt->return_stmt->expr = expr;
        return out;
    }

    AstNode* block = ast_parse_block(parser);
    if(block != null)
        return block;
    
    return null;
}

static AstNode* ast_parse_boolean_and_op(Parser* parser) {
    Token* op_token = parser_chomp_if(parser, AND);
    if(op_token == null)
        return null;
    
    AstNode* out = ast_create_node(AstNodeKindBinaryOpExpr);
    out->data.expr->binary_op_expr->op = BinaryOpKindBoolAnd;
    return out;
}

static AstNode* ast_parse_boolean_or_op(Parser* parser) {
    Token* op_token = parser_chomp_if(parser, OR);
    if(op_token == null)
        return null;
    
    AstNode* out = ast_create_node(AstNodeKindBinaryOpExpr);
    out->data.expr->binary_op_expr->op = BinaryOpKindBoolOr;
    return out;
}

// IfPrefix
//      IfPrefix Expr (KEYWORD(else) Expr)
static AstNode* ast_parse_if_expr(Parser* parser) {
    return ast_parse_if_expr_helper(
        parser, 
        ast_parse_expr
    );
}

// TODO
// LoopExpr
// static AstNode* ast_parse_loop_expr(Parser* parser) {
//     return ast_parse_loop_expr_helper(
//         parser,
//         ast_parse_for_expr,
//         ast_parse_while_expr
//     );
// }

// InitList
//      | LBRACE Expr (COMMA Expr)* COMMA? RBRACE
//      | LBRACE RBRACE
static AstNode* ast_parse_init_list(Parser* parser) {
    Token* lbrace = parser_chomp_if(parser, LBRACE);
    if(lbrace == null)
        return null;
    free(lbrace);

    AstNode* out = ast_create_node(AstNodeKindInitExpr);
    out->data.expr->init_expr->kind = InitExprKindArray;
    out->data.expr->init_expr->entries = vec_new(AstNode, 1);

    AstNode* first = ast_parse_expr(parser);
    if(first != null) {
        vec_push(out->data.expr->init_expr->entries, first);

        Token* comma;
        while((comma = parser_chomp_if(parser, COMMA)) != null) {
            free(comma);
            AstNode* expr = ast_parse_expr(parser);
            if(expr == null)
                break;
            vec_push(out->data.expr->init_expr->entries, expr);
        }

        Token* rbrace = parser_expect_token(parser, RBRACE);
        free(rbrace);
        return out;
    }
    Token* rbrace = parser_expect_token(parser, RBRACE);
    free(rbrace);
    return out;
}

// TypeExpr
//      PrefixTypeOp* SuffixExpr
static AstNode* ast_parse_type_expr(Parser* parser) {
    return ast_parse_prefix_op_expr(
        parser,
        ast_parse_prefix_type_op,
        ast_parse_suffix_expr
    );
}

// SuffixExpr
//      | PrimaryTypeExpr SuffixOp* FuncCallArgs
//      | PrimaryTypeExpr (SuffixOp / FuncCallArguments)*
static AstNode* ast_parse_suffix_expr(Parser* parser) {
    AstNode* out = ast_parse_primary_type_expr(parser);
    if(out == null)
        return null;
    
    while(true) {
        AstNode* suffix = ast_parse_suffix_op(parser);
        if(suffix != null) { 
            switch(suffix->kind) {
                case AstNodeKindSliceExpr:
                    suffix->data.expr->slice_expr->array_ref_expr = out;
                    break;
                // case AstNodeKindArrayAccessExpr:
                //     suffix->data.array_access_expr->array_ref_expr = out;
                //     break;
                default:
                    unreachable();
            }
            out = suffix;
            continue;
        }

        AstNode* call = ast_parse_func_call_args(parser);
        if(call != null) {
            CORETEN_CHECK(call->kind == AstNodeKindFuncCallExpr);
            call->data.expr->func_call_expr->func_call_expr = out;
            out = call;
            continue;
        }
        break;
    } // while(true)

    return out;
}

// PrimaryTypeExpr
//      | CHAR
//      | FLOAT
//      | FuncPrototype
//      | LabeledTypeExpr
//      | IDENT
//      | IfTypeExpr
//      | INTEGER
//      | TypeExpr
//      | KEYWORD(true)
//      | KEYWORD(false)
//      | KEYWORD(null)
//      | KEYWORD(unreachable)
//      | STRING (Literal)
//      | MatchExpr
static AstNode* ast_parse_primary_type_expr(Parser* parser) {
    Token* char_lit = parser_chomp_if(parser, CHAR);
    if(char_lit != null) {
        free(char_lit);
        return ast_create_node(AstNodeKindCharLiteral);
    }

    Token* float_lit = parser_chomp_if(parser, FLOAT_LIT);
    if(float_lit != null) {
        free(float_lit);
        return ast_create_node(AstNodeKindFloatLiteral);
    }

    Token* func_prototype = ast_parse_func_prototype(parser);
    if(func_prototype != null)
        return func_prototype;
    free(func_prototype);

    Token* identifier = parser_chomp_if(parser, IDENTIFIER);
    if(identifier != null) {
        free(identifier);
        return ast_create_node(AstNodeKindIdentifier);
    }

    Token* if_type_expr = ast_parse_if_type_expr(parser);
    if(if_type_expr != null)
        return if_type_expr;
    free(if_type_expr);

    Token* int_lit = parser_chomp_if(parser, INTEGER);
    if(int_lit != null) {
        free(int_lit);
        return ast_create_node(AstNodeKindIntLiteral);
    }
    
    Token* true_token = parser_chomp_if(parser, TOK_TRUE);
    if(true_token != null) {
        free(true_token);
        AstNode* out = ast_create_node(AstNodeKindBoolLiteral);
        out->data.comptime_value->bool_value = true;
        return out;
    }

    Token* false_token = parser_chomp_if(parser, TOK_TRUE);
    if(false_token != null) {
        free(false_token);
        AstNode* out = ast_create_node(AstNodeKindBoolLiteral);
        out->data.comptime_value->bool_value = false;
        return out;
    }

    Token* unreachable_token = parser_chomp_if(parser, UNREACHABLE);
    if(unreachable_token != null) {
        free(unreachable_token);
        return ast_create_node(AstNodeKindUnreachable);
    }

    Token* string_lit = parser_chomp_if(parser, STRING);
    if(string_lit != null) {
        free(string_lit);
        return ast_create_node(AstNodeKindStringLiteral);
    }

    Token* match_token = past_parse_match_expr(parser);
    if(match_token != null)
        return match_token;

    return null;
}

// MatchExpr
//      KEYWORD(match) LPAREN? Expr RPAREN? LBRACE MatchProngList RBRACE
static AstNode* ast_parse_match_expr(Parser* parser {
    Token* match_token = parser_chomp_if(parser, MATCH);
    if(match_token == null)
        return null;
    free(match_token);

    // Left and Right Parenthesis' here are optional
    Token* lparen = parser_chomp_if(parser, LPAREN);
    AstNode* expr = ast_parse_expr(parser);
    Token* rparen = parser_chomp_if(parser, RPAREN);
    free(lparen);
    free(rparen);

    // These *aren't* optional
    Token* lbrace = parser_expect_token(parser, LBRACE);
    Vec* branches = ast_parse_list(parser, COMMA, ast_parse_match_branch);
    Token* rbrace = parser_expect_token(parser, RBRACE);

    AstNode* out = ast_create_node(AstNodeKindMatchExpr);
    out->data.expr->match_expr->expr = expr;
    out->data.expr->match_expr->branches = branches;
    return out;
}

// BreakLabel
//      COLON IDENTIFIER
static Token* ast_parse_break_label(Parser* parser) {
    Token* colon = parser_chomp_if(parser, COLON);
    if(colon == null) {
        return null;
    }
    free(colon);
    Token* ident = parser_expect_token(parser, IDENTIFIER);
    return ident;
}

// BlockLabel
//      IDENTIFIER COLON
static Token* ast_parse_block_label(Parser* parser) {
    Token* ident = parser_chomp_if(parser, IDENTIFIER);
    if(ident == null)
        return null;
    
    Token* colon = parser_chomp_if(parser, COLON);
    if(colon == null) {
        free(ident);
        return colon;
    }
    free(colon);

    return ident;
}

// MatchBranch
//      KEYWORD(case) (COLON? / EQUALS_ARROW?) AssignmentExpr
static AstNode* ast_parse_match_branch(Parser* parser) {
    AstNode* out = ast_parse_match_case_kwd(parser);
    CORETEN_CHECK(out->kind == AstNodeKindMatchBranch);
    if(out == null)
        return null;
    
    Token* colon = parser_chomp_if(parser, COLON); // `:`
    Token* equals_arrow = parser_chomp_if(parser, EQUALS_ARROW); // `=>`
    if(colon == null && equals_arrow == null)
        ast_error(
            "Missing token after `case`. Either `:` or `=>`"
        );
    free(colon);
    free(equals_arrow);

    AstNode* expr = ast_parse_assignment_expr(parser);
    out->data.expr->match_branch->expr = expr;

    return out;
}

// MatchCase
//      | MatchItem (COMMA MatchItem)* COMMA?
//      | KEYWORD(else)
static AstNode* ast_parse_match_case_kwd(Parser* parser) {
    AstNode* match_item = ast_parse_match_item(parser);
    if(match_item != null) {
        AstNode* out = ast_create_node(AstNodeKindMatchBranch);
        vec_push(out->data.expr->match_branch->branches, match_item);

        Token* comma;
        while((comma = parser_chomp_if(parser, COMMA)) != null) {
            free(comma);
            AstNode* item = ast_parse_match_item(parser);
            if(item == null)
                break;
            
            vec_push(out->data.expr->match_branch->branches, item);
        }

        return out;
    }
}