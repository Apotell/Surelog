`define MIN(x, y)
`define MAX(x, y)
`define ADD(a, b, c, d) a + b + c + d
`define ASSERT_DEFAULT_CLK clk_i
`define ASSERT_DEFAULT_RST !rst_ni
`define ASSERT_ERROR(__name)
`define ASSERT(__name, __prop, __clk = `ASSERT_DEFAULT_CLK, __rst = `ASSERT_DEFAULT_RST)
`define ASSERT_IF(__name, __prop, __enable, __clk = `ASSERT_DEFAULT_CLK, __rst = `ASSERT_DEFAULT_RST)
`define ASSERT_STATIC_IN_PACKAGE(__name, __prop)
`define uvm_info(ID, MSG, VERBOSITY)
`define uvm_fatal(ID, MSG)
`define DV_CHECK_RANDOMIZE_WITH_FATAL(a, b, c="")
`define DV_CHECK_STD_RANDOMIZE_WITH_FATAL(a, b, c="")

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

  `DV_CHECK_STD_RANDOMIZE_WITH_FATAL(program_stack_len,
      program_stack_len inside {[cfg.min_stack_len_per_program : cfg.max_stack_len_per_program]};
      // Keep stack len word aligned to avoid unaligned load/store
      program_stack_len % (XLEN/8) == 0;,
      "Cannot randomize program_stack_len")

  `uvm_info(get_full_name(), $sformatf("Start generating %0d instruction",
                              instr_stream.instr_list.size()), UVM_LOW)

  `DV_CHECK_STD_RANDOMIZE_WITH_FATAL(instr_name,
                                     instr_name inside {allowed_instr};)

  `DV_CHECK_RANDOMIZE_WITH_FATAL(illegal_pte,
                                  !(xwr inside {NEXT_LEVEL_PAGE, R_W_EXECUTE_PAGE});)

  `DV_CHECK_RANDOMIZE_WITH_FATAL(valid_leaf_pte,
    // Set the correct privileged mode
    if(privileged_mode == USER_MODE) {
      u == 1'b1;
    } else {
      // Accessing user mode page from supervisor mode is only allowed when MSTATUS.SUM and
      // MSTATUS.MPRV are both 1
      if(!(cfg.mstatus_sum && cfg.mstatus_mprv)) {
   u == 1'b0;
      }
    }
    // Set a,d bit to 1 avoid page/access fault exceptions
    a == 1'b1;
    d == 1'b1;
    // Default: Readable, writable, executable page
    soft xwr == R_W_EXECUTE_PAGE;
    // Page is valid
    v == 1'b1;
  )
endmodule
