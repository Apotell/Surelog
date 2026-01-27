module tb;

  real a, res;

  initial begin
    
    a = 1;
    res = $clog2(a);

    a = 1.0;
    res = $ln(a);

    a = 1.0;
    res = $log10(a);

    a = 1.0;
    res = $exp(a);

    a = 1.0;
    res = $sqrt(a);

    a = 1.1;
    res = $floor(a);

    a = 1.1;
    res = $ceil(a);

    $finish;
  end

endmodule
	
