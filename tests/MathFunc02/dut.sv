module tb;

  real res = 0.0;

  initial begin
    res = $clog2(1);
    res = $ln(1.0);
    res = $log10(1.0);
    res = $exp(1.0);
    res = $sqrt(1.0);
    res = $floor(1.1);
    res = $ceil(1.1);
    $finish;
  end

endmodule
