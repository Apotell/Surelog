module tb;

  int i;
  logic [7:0] l8;
  real r;
  string s = "test";
  
  int bit_count;
  string type_name;

  initial begin
    // $bits: Returns the number of bits in a type or variable
    bit_count = $bits(i);          // 32
    bit_count = $bits(l8);         // 8
    bit_count = $bits(real);       // 64

    // $typename: Returns a string representation of the data type
    type_name = $typename(s);      // "string"
    type_name = $typename(i);      // "int"
    type_name = $typename(logic);  // "logic"
  end

endmodule

