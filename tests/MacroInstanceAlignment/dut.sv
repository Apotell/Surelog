`define MIN(x, y) ((( x ) < ( y )) ? ( x ) : ( y ))
`define MAX(x, y) (((  x   ) > (  y  )) ? (  x  ) : (  y  ))
`define ADD(a, b, c, d) \
`MIN((a), (b))          \
+                       \
            `MAX((c), (d))

// Converts an arbitrary block of code into a Verilog string
`define PRIM_STRINGIFY(__x) `"__x`"

`define ASSERT_DEFAULT_CLK clk_i
`define ASSERT_DEFAULT_RST !rst_ni

`define ASSERT_ERROR(__name)                                                             \
  $error("%0t: (%0s:%0d) [%m] [ASSERT FAILED] %0s", $time, `__FILE__, `__LINE__,         \
         `PRIM_STRINGIFY(__name));                                                       \
            
`define ASSERT(__name, __prop, __clk = `ASSERT_DEFAULT_CLK, __rst = `ASSERT_DEFAULT_RST) \
  __name: assert property (@(posedge __clk) disable iff ((__rst) !== '0) (__prop))       \
    else begin                                                                           \
      `ASSERT_ERROR(__name)                                                              \
    end    

`define ASSERT_IF(__name, __prop, __enable, __clk = `ASSERT_DEFAULT_CLK, __rst = `ASSERT_DEFAULT_RST) \
  `ASSERT(__name, (__enable) |-> (__prop), __clk, __rst)

`define ASSERT_STATIC_IN_PACKAGE(__name, __prop)              \
  function automatic bit assert_static_in_package_``__name(); \
    bit unused_bit [((__prop) ? 1 : -1)];                     \
    unused_bit = '{default: 1'b0};                            \
    return unused_bit[0];                                     \
  endfunction
`endif  // ASSERT_SV
    
module m;
  assign xyz0 = `ADD(  1000000, 
  500000,
  2000000000, 
  100000 );

  assign xyz1 = `ADD(  100000000000000000000000, 
  500000000000000,
  2000000000000000000000000000000000000000, 
  100000 );

  assign xyz2 = `ADD(  1000000000000000000000000000000000000000000000000000000000000, 
  500000000000000,
  2000000000000000000000000000000000000000, 
  100000 );

  assign xyz3 = `ADD(  1000000000000000000000000000000000000000000000000000000000000, 
  500000000000000,
  20000000000000000000000000000000000000000000000000000000000000000000000, 
  100000 );

  assign xyz4 = `ADD(  1000000000000000000000000000000000000000000000000000000000000, 
  500000000000000000000000000000000000000000000000000000000000,
  2000000000000000000000000, 
  100000 );

  assign xyz5 = `ADD(  1000000, 
  500000,  //Test
  2000000000, 
  100000 );

  assign xyz6 = `ADD(  1000000, 
  500000,  //Test
  2000000000,  // Test
  100000 );

  assign xyz7 = `ADD(  1000000,  //Test
  500000,  //Test
  2000000000,  // Test
  100000 );

  assign xyz8 = `ADD(  1000000  //Test
  ,500000  //Test
  ,2000000000  // Test
  ,100000 );
  
  assign xyz9 = `ADD(  1000000, // comment
  500000,
  //Test
  2000000000, 
  // another comment
  100000 );
  
  assign xyz10 = `ADD(  1000000, // comment
  500000,
  2000000000000000000000000000000000000000000000000000000000000000000000000, 
  // another comment
  100000 );

  assign xyz11 = `ADD(  1000000, // comment
  500000,
  "This is a very long argument which should cause the macro instance to not be formatted as expected", 
  // another comment
  100000 );

  assign xyz12 = `ADD(  1000000,
  500000 +
    100000000000 +
            20000000000000000,
  10, 
  100000 );
  
  `ASSERT(IbexInstrLSBsKnown, valid_i |->
      !$isunknown(instr_i[1:0]))
      
  `ASSERT(IbexC0Known1, (valid_i && (instr_i[1:0] == 2'b00)) |->
      !$isunknown(instr_i[15:13]))
      
  `ASSERT(IbexC1Known2, (valid_i && (instr_i[1:0] == 2'b01) && (instr_i[15:13] == 3'b100)) |->
      !$isunknown(instr_i[11:10]))
      
  `ASSERT(IbexC1Known3, (valid_i &&
      (instr_i[1:0] == 2'b01) && (instr_i[15:13] == 3'b100) && (instr_i[11:10] == 2'b11)) |->
      !$isunknown({instr_i[12], instr_i[6:5]}))
      
  `ASSERT(IbexC2Known1, (valid_i && (instr_i[1:0] == 2'b10)) |->
      !$isunknown(instr_i[15:13]))
      
  `ASSERT_IF(IbexExceptionPrioOnehot,
             $onehot({instr_fetch_err_prio,
                      illegal_insn_prio,
                      ecall_insn_prio,
                      ebrk_insn_prio,
                      store_err_prio,
                      load_err_prio}),
             (ctrl_fsm_cs == FLUSH) & exc_req_q)

  `ASSERT_STATIC_IN_PACKAGE(ThisNameDoesNotMatter1, 32 == $bits(pkg_b::ParameterIntEqual4))
  
  `ASSERT_STATIC_IN_PACKAGE(ThisNameDoesNotMatter2, $bits(ParameterIntInPkgA) == 32)

  `ASSERT_STATIC_IN_PACKAGE(ThisNameDoesNotMatter3, $bits(ParameterIntInPkgA) == $bits(pkg_b::ParameterIntEqual4))
  
  `ASSERT_STATIC_IN_PACKAGE(
      ThisNameDoesNotMatter4,
      $bits(ParameterIntInPkgA) == $bits(pkg_b::ParameterIntEqual4))
endmodule
