module tb;
  real r;
  shortreal sr;
  logic [63:0] l64;
  logic [31:0] l32;

  initial begin
    // 64-bit conversions (Double Precision)
    r   = $bitstoreal(64'h3FF0000000000000);    // Bit pattern to real
    l64 = $realtobits(2.0);    // Real to bit pattern

    // 32-bit conversions (Single Precision)
    sr  = $bitstoshortreal(32'h3F800000);       // Bit pattern to shortreal
    l32 = $shortrealtobits(shortreal'(1.0)); // Shortreal to bit pattern
  end
endmodule
