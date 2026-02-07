// Code your testbench here
// or browse Examples
module top();
  initial begin
    real res = 0.0;

    res = $asin(0.0);
    res = $asin(1.0);
    res = $asin(-1.0);
    res = $asin(0.5);
    res = $asin(-0.5);
    res = $asin(1e-10);
    res = $asin(2.0);

    res = $acos(1.0);
    res = $acos(-1.0);
    res = $acos(0.0);
    res = $acos(0.5);
    res = $acos(-0.5);
    res = $acos(1e-12);
    res = $acos(-2.0);

    res = $atan(0.0);
    res = $atan(1.0);
    res = $atan(-1.0);
    res = $atan(1e10);
    res = $atan(-1e10);
    res = $atan(1e-10);

    res = $atan2(0.0, 1.0);
    res = $atan2(1.0, 0.0);
    res = $atan2(-1.0, 0.0);
    res = $atan2(1.0, 1.0);
    res = $atan2(-1.0, -1.0);
    res = $atan2(1e308, 1.0);
    res = $atan2(0.0, 0.0);

    res = $hypot(0.0, 0.0);
    res = $hypot(3.0, 4.0);
    res = $hypot(-3.0, 4.0);
    res = $hypot(1e154, 1e154);
    res = $hypot(1e308, 1.0);
    res = $hypot(1e308, 1e308);

    res = $pow(2.0, 8.0);
    res = $pow(10.0, 0.0);
    res = $pow(0.0, 10.0);
    res = $pow(2.0, 3.0);
    res = $pow(-2.0, 0.5);
    res = $pow(1e154, 2.0);
    res = $pow(1e200, 2.0);

    res = $sinh(0.0);
    res = $sinh(1.0);
    res = $sinh(-1.0);
    res = $sinh(10.0);
    res = $sinh(-10.0);
    res = $sinh(700.0);
    res = $sinh(710.0);

    res = $cosh(0.0);
    res = $cosh(1.0);
    res = $cosh(-1.0);
    res = $cosh(10.0);
    res = $cosh(700.0);
    res = $cosh(-700.0);
    res = $cosh(710.0);

    res = $tanh(0.0);
    res = $tanh(1.0);
    res = $tanh(-1.0);
    res = $tanh(10.0);
    res = $tanh(-10.0);
    res = $tanh(700.0);
    res = $tanh(-700.0);

    res = $sin(0.0);
    res = $sin(1.570796);
    res = $sin(-1.570796);
    res = $sin(3.141593);
    res = $sin(1e10);
    res = $sin(-1e10);
    res = $sin(1e-10);

    res = $asinh(0.0);
    res = $asinh(1.0);
    res = $asinh(-1.0);
    res = $asinh(10.0);
    res = $asinh(-10.0);
    res = $asinh(1e20);
    res = $asinh(1e308);

    res = $cos(0.0);
    res = $cos(1.570796);
    res = $cos(3.141593);
    res = $cos(-3.141593);
    res = $cos(1e10);
    res = $cos(-1e10);
    res = $cos(1e-10);

    res = $acosh(1.0);
    res = $acosh(2.0);
    res = $acosh(10.0);
    res = $acosh(1e10);
    res = $acosh(1e308);
    res = $acosh(0.5);
    res = $acosh(-10.0);

    res = $tan(0.0);
    res = $tan(0.785398);
    res = $tan(-0.785398);
    res = $tan(1.570796);
    res = $tan(-1.570796);
    res = $tan(1e10);
    res = $tan(1e-10);

    res = $atanh(0.0);
    res = $atanh(0.5);
    res = $atanh(-0.5);
    res = $atanh(0.99);
    res = $atanh(-0.99);
    res = $atanh(1.0);
    res = $atanh(-1.0);
  end
endmodule
