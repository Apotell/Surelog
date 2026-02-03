module tb();
  
  initial begin
    real a, b, res;

    a = 2.0;
    res = $asin(a);

    a = 1.0;
    res = $acos(a);

    a = 1.0;
    res = $atan(a);

    a = 1.0; b = 1.0;
    res = $atan2(a, b);

    a = 3.0; b = 4.0;
    res = $hypot(a, b);

    a = 2.0; b = 8.0;
    res = $pow(a, b);

    a = 10.0;
    res = $sinh(a);

    a = 1.0;
    res = $cosh(a);

    a = 1.0;
    res = $tanh(a);

    a = 1.570796;
    res = $sin(a);

    a = 1.0;
    res = $asinh(a);

    a = 1.570796;
    res = $cos(a);

    a = 1.0;
    res = $acosh(a);

    a = 0.785398;
    res = $tan(a);

    a = 0.5;
    res = $atanh(a);

  end

endmodule


