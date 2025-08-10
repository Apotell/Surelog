`ifndef DUT_SV


`define DUT_SV
// The lines between ifndef & define above should be stripped.

`include "defines.svh"  // Include macros at global scope

// Ifdefs shouldn't be indented but the content should be
// The lines between ifdef, define and endif below should be stripped
`ifdef ifdef1



`define ifdef1 10
`endif

`ifdef ifdef2
`define ifdef2 20





`endif

`define MACRO_1 1

package my_pkg;
typedef enum logic [1:0] {IDLE, BUSY, DONE} state_t;
parameter int MY_PARAM = 100;
`define MACRO_2 2
endpackage

module top;
 import my_pkg::*;  // Import everything from my_pkg
 `define LENGTH_D
 `define MACRO_3 3
 
// following undef should be on consecutive lines
`undef MACRO_1



`undef MACRO_3
 // The defines and undef above should be grouped into two separate blocks with a line between them
 
//ifdef
`ifdef WIDTH_A
`define WIDTH 10
`endif

//nested ifdef
`ifdef HEIGHT_A
`define HEIGHT \
  10 + 10
`ifdef HEIGHT_B
`define HEIGHT \
  10 + 10 + 10
`endif
`endif

//ifdef-elseif
`ifdef LENGTH_A
`define LENGTH \
  10 + \
  10 + \
  10 + \
  10
`elseif LENGTH_B
`define LENGTH \
  100 - \
  50
`endif

//ifdef-elsif
`ifdef LENGTH_C
`define LENGTH 40
`elsif LENGTH_D
`define LENGTH 50
`else
`define LENGTH 60
`endif

//nested if-elseif in if
`ifdef ALPHA_A
`define ALPHA 60
`ifdef ALPHA_IF_A
`define ALPHA 70
`elseif ALPHA_ELSE_A
`define ALPHA 80
`endif
`elsif ALPHA_B
`define ALPHA 90
`elseif ALPHA_C
`define ALPHA 100
`else
`define ALPHA 110
`endif

//nested if-elseif in else
`ifdef BETA_A
`define BETA 100
`elseif BETA_B
`define BETA 110
`ifdef BETA_IF_B
`define BETA 120
`elseif BETA_ELSE_B
`define BETA 130
`elsif BETA_ELSE_C
`define BETA 140
`else
`define BETA 150
`endif
`endif

 initial begin
 int high = `RESET_ACTIVE_HIGH; //declare variable high
 int low = `RESET_ACTIVE_lOW; //declare variable low
 end

 //nested if-elseif
`ifdef A
`define HELLO_A 200
`elseif B
`define HELLO_B 300
`ifdef IF_B
`define HELLO 400
`elseif ELSE_B
`define HELLO 500
`ifdef C
`define HELLO_C 600
`elsif ELS_C
`define HELLO_D 700
`endif
`endif
`else
`define HELLO_E 800
`endif

endmodule

`endif  // DUT_SV