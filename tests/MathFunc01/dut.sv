module tb();

  initial begin
    real res = 0.0;

    res = $asin(2.0);
    res = $acos(1.0);
    res = $atan(1.0);
    res = $atan2(1.0, 1.0);
    res = $hypot(3.0, 4.0);
    res = $pow(2.0, 8.0);
    res = $sinh(10.0);
    res = $cosh(1.0);
    res = $tanh(1.0);
    res = $sin(1.570796);
    res = $asinh(1.0);
    res = $cos(1.570796);
    res = $acosh(1.0);
    res = $tan(0.785398);
    res = $atanh(0.5);
  end

endmodule
