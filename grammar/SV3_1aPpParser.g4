/*
 Copyright 2019 Alain Dargelas

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

parser grammar SV3_1aPpParser;

options {
  tokenVocab = SV3_1aPpLexer;
}

top_level_rule: null_rule source_text EOF; // SV files

source_text: description*;

null_rule
  :
  ; // Placeholder rule that create the "0" VObject, DO NOT REMOVE

description
  : escaped_identifier
  | unterminated_string
  | string
  | integral_number
  | macro_definition
  | comment
  | celldefine_directive
  | endcelldefine_directive
  | default_nettype_directive
  | undef_directive
  | ifdef_directive
  | ifndef_directive
  | else_directive
  | elsif_directive
  | elseif_directive
  | endif_directive
  | include_directive
  | resetall_directive
  | begin_keywords_directive
  | end_keywords_directive
  | timescale_directive
  | unconnected_drive_directive
  | nounconnected_drive_directive
  | line_directive
  | default_decay_time_directive
  | default_trireg_strength_directive
  | delay_mode_distributed_directive
  | delay_mode_path_directive
  | delay_mode_unit_directive
  | delay_mode_zero_directive
  | protect_directive
  | endprotect_directive
  | protected_directive
  | endprotected_directive
  | expand_vectornets_directive
  | noexpand_vectornets_directive
  | autoexpand_vectornets_directive
  | remove_gatename_directive
  | noremove_gatenames_directive
  | remove_netname_directive
  | noremove_netnames_directive
  | accelerate_directive
  | noaccelerate_directive
  | undefineall_directive
  | uselib_directive
  | disable_portfaults_directive
  | enable_portfaults_directive
  | nosuppress_faults_directive
  | suppress_faults_directive
  | signed_directive
  | unsigned_directive
  | pragma_directive
  | sv_file_directive
  | sv_line_directive
  | macro_instance
  | module
  | endmodule
  | sv_interface
  | endinterface
  | program
  | endprogram
  | primitive
  | endprimitive
  | sv_package
  | endpackage
  | checker
  | endchecker
  | config
  | endconfig
  | text_blob
  ;

escaped_identifier: ESCAPED_IDENTIFIER;

macro_instance
  : (MACRO_IDENTIFIER | MACRO_ESCAPED_IDENTIFIER) SPACES* OPEN_PARENS macro_actual_args CLOSE_PARENS
      # MacroInstanceWithArgs
  | (MACRO_IDENTIFIER | MACRO_ESCAPED_IDENTIFIER) # MacroInstanceNoArgs
  ;

unterminated_string: DOUBLE_QUOTE string_blob* CR;

macro_actual_args: macro_arg* (COMMA macro_arg*)*;

comment: LINE_COMMENT | BLOCK_COMMENT | SYNOPSIS_BLOCK_COMMENT;

integral_number: INTEGRAL_NUMBER;

pound_delay: POUND_DELAY;

pound_pound_delay: POUND_POUND_DELAY;

macro_definition
  : define_directive
  | multiline_args_macro_definition
  | simple_no_args_macro_definition
  | multiline_no_args_macro_definition
  | simple_args_macro_definition
  ;

include_directive
  : TICK_INCLUDE SPACES (
    QUOTED_STRING
    | SIMPLE_IDENTIFIER
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

line_directive: TICK_LINE SPACES integral_number QUOTED_STRING SPACES integral_number;

default_nettype_directive
  : TICK_DEFAULT_NETTYPE SPACES SIMPLE_IDENTIFIER
  ;

sv_file_directive: TICK_FILE__;

sv_line_directive: TICK_LINE__;

timescale_directive: TICK_TIMESCALE TIMESCALE;

undef_directive
  : TICK_UNDEF SPACES (
    SIMPLE_IDENTIFIER
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

ifdef_directive
  : TICK_IFDEF SPACES (
    SIMPLE_IDENTIFIER
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

ifdef_directive_in_macro_body
  : TICK_IFDEF SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

ifndef_directive
  : TICK_IFNDEF SPACES (
    SIMPLE_IDENTIFIER
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

ifndef_directive_in_macro_body
  : TICK_IFNDEF SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

elsif_directive
  : TICK_ELSIF SPACES (
    SIMPLE_IDENTIFIER
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

elsif_directive_in_macro_body
  : TICK_ELSIF SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

elseif_directive
  : TICK_ELSEIF SPACES (
    SIMPLE_IDENTIFIER
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

elseif_directive_in_macro_body
  : TICK_ELSEIF SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
    | macro_instance
  )
  ;

else_directive: TICK_ELSE;

endif_directive
  : TICK_ENDIF SPACES* LINE_COMMENT
  | TICK_ENDIF
  ;

resetall_directive: TICK_RESETALL;

begin_keywords_directive: TICK_BEGIN_KEYWORDS SPACES QUOTED_STRING;

end_keywords_directive: TICK_END_KEYWORDS;

pragma_directive
  : TICK_PRAGMA SPACES SIMPLE_IDENTIFIER (
    pragma_expression (SPECIAL pragma_expression)*
  )*
  ;

celldefine_directive: TICK_CELLDEFINE SPACES* CR;

endcelldefine_directive: TICK_ENDCELLDEFINE SPACES* CR;

protect_directive: TICK_PROTECT SPACES* CR;

endprotect_directive: TICK_ENDPROTECT SPACES* CR;

protected_directive: TICK_PROTECTED;

endprotected_directive: TICK_ENDPROTECTED;

expand_vectornets_directive: TICK_EXPAND_VECTORNETS;

noexpand_vectornets_directive: TICK_NOEXPAND_VECTORNETS;

autoexpand_vectornets_directive: TICK_AUTOEXPAND_VECTORNETS;

uselib_directive: TICK_USELIB text_blob+;

disable_portfaults_directive: TICK_DISABLE_PORTFAULTS;

enable_portfaults_directive: TICK_ENABLE_PORTFAULTS;

nosuppress_faults_directive: TICK_NOSUPPRESS_FAULTS;

suppress_faults_directive: TICK_SUPPRESS_FAULTS;

signed_directive: TICK_SIGNED;

unsigned_directive: TICK_UNSIGNED;

remove_gatename_directive: TICK_REMOVE_GATENAME;

noremove_gatenames_directive: TICK_NOREMOVE_GATENAMES;

remove_netname_directive: TICK_REMOVE_NETNAME;

noremove_netnames_directive: TICK_NOREMOVE_NETNAMES;

accelerate_directive: TICK_ACCELERATE;

noaccelerate_directive: TICK_NOACCELERATE;

default_trireg_strength_directive
  : TICK_DEFAULT_TRIREG_STRENGTH SPACES integral_number
  ;

default_decay_time_directive
  : TICK_DEFAULT_DECAY_TIME SPACES (
    integral_number
    | SIMPLE_IDENTIFIER
    | FIXED_POINT_NUMBER
  )
  ;

unconnected_drive_directive
  : TICK_UNCONNECTED_DRIVE SPACES SIMPLE_IDENTIFIER
  ;

nounconnected_drive_directive
  : TICK_NOUNCONNECTED_DRIVE SPACES* CR
  ;

delay_mode_distributed_directive: TICK_DELAY_MODE_DISTRIBUTED;

delay_mode_path_directive: TICK_DELAY_MODE_PATH;

delay_mode_unit_directive: TICK_DELAY_MODE_UNIT;

delay_mode_zero_directive: TICK_DELAY_MODE_ZERO;

undefineall_directive: TICK_UNDEFINEALL;

module: MODULE;
endmodule: ENDMODULE;

sv_interface: INTERFACE;
endinterface: ENDINTERFACE;

program: PROGRAM;
endprogram: ENDPROGRAM;

primitive: PRIMITIVE;
endprimitive: ENDPRIMITIVE;

sv_package: PACKAGE;
endpackage: ENDPACKAGE;

checker: CHECKER;
endchecker: ENDCHECKER;

config: CONFIG;
endconfig: ENDCONFIG;

define_directive
  : TICK_DEFINE SPACES (SIMPLE_IDENTIFIER | ESCAPED_IDENTIFIER) SPACES* CR
  ;

multiline_no_args_macro_definition
  : TICK_DEFINE SPACES (SIMPLE_IDENTIFIER | ESCAPED_IDENTIFIER) SPACES*
      escaped_macro_definition_body
  ;

multiline_args_macro_definition
  : TICK_DEFINE SPACES (SIMPLE_IDENTIFIER | ESCAPED_IDENTIFIER) macro_arguments SPACES*
      escaped_macro_definition_body
  ;

simple_no_args_macro_definition
  : TICK_DEFINE SPACES (SIMPLE_IDENTIFIER | ESCAPED_IDENTIFIER) SPACES simple_macro_definition_body
      (CR | LINE_COMMENT)
  | TICK_DEFINE SPACES (SIMPLE_IDENTIFIER | ESCAPED_IDENTIFIER) SPACES* CR
  ;

simple_args_macro_definition
  : TICK_DEFINE SPACES (SIMPLE_IDENTIFIER | ESCAPED_IDENTIFIER) macro_arguments SPACES
      simple_macro_definition_body (CR | LINE_COMMENT)
  | TICK_DEFINE SPACES (SIMPLE_IDENTIFIER | ESCAPED_IDENTIFIER) macro_arguments SPACES* CR
  ;

identifier_in_macro_body: (SIMPLE_IDENTIFIER TICK_TICK?)*;

simple_no_args_macro_definition_in_macro_body
  : TICK_DEFINE SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
  ) SPACES simple_macro_definition_body_in_macro_body
  | TICK_DEFINE SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
  ) SPACES*
  | TICK_DEFINE SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
  ) TICK_VARIABLE simple_macro_definition_body_in_macro_body
  ;

simple_args_macro_definition_in_macro_body
  : TICK_DEFINE SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
  ) macro_arguments SPACES simple_macro_definition_body_in_macro_body
  | TICK_DEFINE SPACES (
    identifier_in_macro_body
    | ESCAPED_IDENTIFIER
  ) macro_arguments
  ;

directive_in_macro
  : celldefine_directive
  | endcelldefine_directive
  | default_nettype_directive
  | undef_directive
  | ifdef_directive
  | ifndef_directive
  | else_directive
  | elsif_directive
  | elseif_directive
  | endif_directive
  | include_directive
  | resetall_directive
  | timescale_directive
  | unconnected_drive_directive
  | nounconnected_drive_directive
  | line_directive
  | default_decay_time_directive
  | default_trireg_strength_directive
  | delay_mode_distributed_directive
  | delay_mode_path_directive
  | delay_mode_unit_directive
  | delay_mode_zero_directive
  | protect_directive
  | endprotect_directive
  | protected_directive
  | endprotected_directive
  | expand_vectornets_directive
  | noexpand_vectornets_directive
  | autoexpand_vectornets_directive
  | remove_gatename_directive
  | noremove_gatenames_directive
  | remove_netname_directive
  | noremove_netnames_directive
  | accelerate_directive
  | noaccelerate_directive
  | undefineall_directive
  | uselib_directive
  | disable_portfaults_directive
  | enable_portfaults_directive
  | nosuppress_faults_directive
  | suppress_faults_directive
  | signed_directive
  | unsigned_directive
  | sv_file_directive
  | sv_line_directive
  | sv_package
  | endpackage
  | module
  | endmodule
  | sv_interface
  | endinterface
  | program
  | endprogram
  | primitive
  | endprimitive
  | checker
  | endchecker
  | config
  | endconfig
  | simple_args_macro_definition_in_macro_body
  //(ESCAPED_CR | CR) Adds long runtime on Verilator t_preproc.v case but fixes Syntax Error
  | simple_no_args_macro_definition_in_macro_body //(ESCAPED_CR | CR)
  | pound_delay
  | pound_pound_delay
  ;

macro_arguments
  : OPEN_PARENS (
    (
      SPACES* SIMPLE_IDENTIFIER SPACES* (ASSIGN_OP default_value*)*
    ) (
      COMMA SPACES* (
        SIMPLE_IDENTIFIER SPACES* (ASSIGN_OP default_value*)*
      )
    )*
  )* CLOSE_PARENS
  ;

escaped_macro_definition_body
  : escaped_macro_definition_body_alt1
  | escaped_macro_definition_body_alt2
  ;

escaped_macro_definition_body_alt1
  : (
    unterminated_string
    | MACRO_IDENTIFIER
    | MACRO_ESCAPED_IDENTIFIER
    | escaped_identifier
    | SIMPLE_IDENTIFIER
    | integral_number
    | TEXT_CR
    | pound_delay
    | pound_pound_delay
    | ESCAPED_CR
    | OPEN_PARENS
    | CLOSE_PARENS
    | COMMA
    | ASSIGN_OP
    | DOUBLE_QUOTE
    | TICK_VARIABLE
    | directive_in_macro
    | SPACES
    | FIXED_POINT_NUMBER
    | QUOTED_STRING
    | comment
    | TICK_QUOTE
    | TICK_BACKSLASH_TICK_QUOTE
    | TICK_TICK
    | SPECIAL
    | OPEN_CURLY
    | CLOSE_CURLY
    | OPEN_BRACKET
    | CLOSE_BRACKET
  )*? ESCAPED_CR SPACES* (CR | EOF)
  ;

escaped_macro_definition_body_alt2
  : (
    unterminated_string
    | MACRO_IDENTIFIER
    | MACRO_ESCAPED_IDENTIFIER
    | escaped_identifier
    | SIMPLE_IDENTIFIER
    | integral_number
    | TEXT_CR
    | pound_delay
    | pound_pound_delay
    | ESCAPED_CR
    | OPEN_PARENS
    | CLOSE_PARENS
    | COMMA
    | ASSIGN_OP
    | DOUBLE_QUOTE
    | TICK_VARIABLE
    | directive_in_macro
    | SPACES
    | FIXED_POINT_NUMBER
    | QUOTED_STRING
    | comment
    | TICK_QUOTE
    | TICK_BACKSLASH_TICK_QUOTE
    | TICK_TICK
    | SPECIAL
    | OPEN_CURLY
    | CLOSE_CURLY
    | OPEN_BRACKET
    | CLOSE_BRACKET
  )*? (CR SPACES* | EOF)
  ;

simple_macro_definition_body
  : (
    unterminated_string
    | MACRO_IDENTIFIER
    | MACRO_ESCAPED_IDENTIFIER
    | escaped_identifier
    | SIMPLE_IDENTIFIER
    | integral_number
    | pound_delay
    | pound_pound_delay
    | TEXT_CR
    | OPEN_PARENS
    | CLOSE_PARENS
    | COMMA
    | ASSIGN_OP
    | DOUBLE_QUOTE
    | TICK_VARIABLE
    | SPACES
    | FIXED_POINT_NUMBER
    | QUOTED_STRING
    | comment
    | TICK_QUOTE
    | TICK_BACKSLASH_TICK_QUOTE
    | TICK_TICK
    | SPECIAL
    | OPEN_CURLY
    | CLOSE_CURLY
    | OPEN_BRACKET
    | CLOSE_BRACKET
    | TICK_INCLUDE
    | directive_in_macro
  )*?
  ;

simple_macro_definition_body_in_macro_body
  : (
    unterminated_string
    | MACRO_IDENTIFIER
    | MACRO_ESCAPED_IDENTIFIER
    | escaped_identifier
    | SIMPLE_IDENTIFIER
    | integral_number
    | pound_delay
    | pound_pound_delay
    | TEXT_CR
    | OPEN_PARENS
    | CLOSE_PARENS
    | COMMA
    | ASSIGN_OP
    | DOUBLE_QUOTE
    | TICK_VARIABLE
    | SPACES
    | FIXED_POINT_NUMBER
    | QUOTED_STRING
    | comment
    | TICK_QUOTE
    | TICK_BACKSLASH_TICK_QUOTE
    | TICK_TICK
    | SPECIAL
    | OPEN_CURLY
    | CLOSE_CURLY
    | OPEN_BRACKET
    | CLOSE_BRACKET
  )*?
  ;

pragma_expression
  : SIMPLE_IDENTIFIER
  | integral_number
  | SPACES
  | FIXED_POINT_NUMBER
  | QUOTED_STRING
  | OPEN_CURLY
  | CLOSE_CURLY
  | OPEN_BRACKET
  | CLOSE_BRACKET
  | OPEN_PARENS
  | CLOSE_PARENS
  | COMMA
  | ASSIGN_OP
  | DOUBLE_QUOTE
  | escaped_identifier
  | pound_delay
  | pound_pound_delay
  | SPECIAL
  | ANY
  ;

macro_arg
  : SIMPLE_IDENTIFIER
  | integral_number
  | SPACES
  | FIXED_POINT_NUMBER
  | QUOTED_STRING
  | paired_parens
  | ASSIGN_OP
  | DOUBLE_QUOTE
  | macro_instance
  | CR
  | TEXT_CR
  | escaped_identifier
  | simple_args_macro_definition_in_macro_body
  | simple_no_args_macro_definition_in_macro_body
  | comment
  | pound_delay
  | pound_pound_delay
  | SPECIAL
  | ANY
  ;

paired_parens
  : (
    OPEN_PARENS (
      SIMPLE_IDENTIFIER
      | integral_number
      | SPACES
      | FIXED_POINT_NUMBER
      | QUOTED_STRING
      | COMMA
      | ASSIGN_OP
      | DOUBLE_QUOTE
      | macro_instance
      | TEXT_CR
      | CR
      | paired_parens
      | escaped_identifier
      | comment
      | SPECIAL
      | ANY
    )* CLOSE_PARENS
  )
  | (
    OPEN_CURLY (
      SIMPLE_IDENTIFIER
      | integral_number
      | SPACES
      | FIXED_POINT_NUMBER
      | QUOTED_STRING
      | COMMA
      | ASSIGN_OP
      | DOUBLE_QUOTE
      | macro_instance
      | CR
      | paired_parens
      | escaped_identifier
      | comment
      | SPECIAL
      | ANY
    )* CLOSE_CURLY
  )
  | (
    OPEN_BRACKET (
      SIMPLE_IDENTIFIER
      | integral_number
      | SPACES
      | FIXED_POINT_NUMBER
      | QUOTED_STRING
      | COMMA
      | ASSIGN_OP
      | DOUBLE_QUOTE
      | macro_instance
      | CR
      | paired_parens
      | escaped_identifier
      | comment
      | SPECIAL
      | ANY
    )* CLOSE_BRACKET
  )
  ;

text_blob
  : SIMPLE_IDENTIFIER
  | CR
  | SPACES
  | FIXED_POINT_NUMBER
  | ESCAPED_CR
  | QUOTED_STRING
  | OPEN_PARENS
  | CLOSE_PARENS
  | COMMA
  | ASSIGN_OP
  | DOUBLE_QUOTE
  | OPEN_CURLY
  | CLOSE_CURLY
  | OPEN_BRACKET
  | CLOSE_BRACKET
  | TICK_TICK
  | TICK_VARIABLE
  | TIMESCALE
  | pound_delay
  | pound_pound_delay
  | TICK_QUOTE
  | TICK_BACKSLASH_TICK_QUOTE
  | TEXT_CR
  | SPECIAL
  | ANY
  ;

string: QUOTED_STRING;

default_value
  : SIMPLE_IDENTIFIER
  | integral_number
  | SPACES
  | FIXED_POINT_NUMBER
  | QUOTED_STRING
  | OPEN_CURLY
  | CLOSE_CURLY
  | OPEN_BRACKET
  | CLOSE_BRACKET
  | paired_parens
  | escaped_identifier
  | macro_instance
  | SPECIAL
  | ANY
  ;

string_blob
  : SIMPLE_IDENTIFIER
  | integral_number
  | SPACES
  | FIXED_POINT_NUMBER
  | ESCAPED_CR
  | OPEN_PARENS
  | CLOSE_PARENS
  | COMMA
  | ASSIGN_OP
  | DOUBLE_QUOTE
  | OPEN_CURLY
  | CLOSE_CURLY
  | OPEN_BRACKET
  | CLOSE_BRACKET
  | escaped_identifier
  | TIMESCALE
  | pound_delay
  | pound_pound_delay
  | TEXT_CR
  | SPECIAL
  | ANY
  ;
