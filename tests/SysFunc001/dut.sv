// Code your testbench here
// or browse Examples
module top();
  
  initial begin
    real a, b, res;

    a = 0.0;
    res = $asin(a);
    $display("MathFunc088 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $asin(a);
    $display("MathFunc089 :: a : ", a, ", res : ", res);

    a = -1.0;
    res = $asin(a);
    $display("MathFunc090 :: a : ", a, ", res : ", res);

    a = 0.5;
    res = $asin(a);
    $display("MathFunc091 :: a : ", a, ", res : ", res);

    a = -0.5;
    res = $asin(a);
    $display("MathFunc092 :: a : ", a, ", res : ", res);

    a = 1e-10;
    res = $asin(a);
    $display("MathFunc093 :: a : ", a, ", res : ", res);

    a = 2.0;
    res = $asin(a);
    $display("MathFunc094 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $acos(a);
    $display("MathFunc095 :: a : ", a, ", res : ", res);

    a = -1.0;
    res = $acos(a);
    $display("MathFunc096 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $acos(a);
    $display("MathFunc097 :: a : ", a, ", res : ", res);

    a = 0.5;
    res = $acos(a);
    $display("MathFunc098 :: a : ", a, ", res : ", res);

    a = -0.5;
    res = $acos(a);
    $display("MathFunc099 :: a : ", a, ", res : ", res);

    a = 1e-12;
    res = $acos(a);
    $display("MathFunc100 :: a : ", a, ", res : ", res);

    a = -2.0;
    res = $acos(a);
    $display("MathFunc101 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $atan(a);
    $display("MathFunc102 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $atan(a);
    $display("MathFunc103 :: a : ", a, ", res : ", res);

    a = -1.0;
    res = $atan(a);
    $display("MathFunc104 :: a : ", a, ", res : ", res);

    a = 1e10;
    res = $atan(a);
    $display("MathFunc105 :: a : ", a, ", res : ", res);

    a = -1e10;
    res = $atan(a);
    $display("MathFunc106 :: a : ", a, ", res : ", res);

    a = 1e-10;
    res = $atan(a);
    $display("MathFunc107 :: a : ", a, ", res : ", res);

    a = 0.0; b = 1.0;
    res = $atan2(a, b);
    $display("MathFunc108 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1.0; b = 0.0;
    res = $atan2(a, b);
    $display("MathFunc109 :: a : ", a, ", b : ", b, ", res : ", res);

    a = -1.0; b = 0.0;
    res = $atan2(a, b);
    $display("MathFunc110 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1.0; b = 1.0;
    res = $atan2(a, b);
    $display("MathFunc111 :: a : ", a, ", b : ", b, ", res : ", res);

    a = -1.0; b = -1.0;
    res = $atan2(a, b);
    $display("MathFunc112 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1e308; b = 1.0;
    res = $atan2(a, b);
    $display("MathFunc113 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 0.0; b = 0.0;
    res = $atan2(a, b);
    $display("MathFunc114 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 0.0; b = 0.0;
    res = $hypot(a, b);
    $display("MathFunc115 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 3.0; b = 4.0;
    res = $hypot(a, b);
    $display("MathFunc116 :: a : ", a, ", b : ", b, ", res : ", res);

    a = -3.0; b = 4.0;
    res = $hypot(a, b);
    $display("MathFunc117 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1e154; b = 1e154;
    res = $hypot(a, b);
    $display("MathFunc118 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1e308; b = 1.0;
    res = $hypot(a, b);
    $display("MathFunc119 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1e308; b = 1e308;
    res = $hypot(a, b);
    $display("MathFunc120 :: a : ", a, ", b : ", b, ", res : ", res);
    
    a = 2.0; b = 8.0;
    res = $pow(a, b);
    $display("MathFunc121 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 10.0; b = 0.0;
    res = $pow(a, b);
    $display("MathFunc122 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 0.0; b = 10.0;
    res = $pow(a, b);
    $display("MathFunc123 :: a : ", a, ", b : ", b, ", res : ", res);

    a = -2.0; b = 3.0;
    res = $pow(a, b);
    $display("MathFunc124 :: a : ", a, ", b : ", b, ", res : ", res);

    a = -2.0; b = 0.5;
    res = $pow(a, b);
    $display("MathFunc125 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1e154; b = 2.0;
    res = $pow(a, b);
    $display("MathFunc126 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 1e200; b = 2.0;
    res = $pow(a, b);
    $display("MathFunc127 :: a : ", a, ", b : ", b, ", res : ", res);

    a = 0.0;
    res = $sinh(a);
    $display("MathFunc128 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $sinh(a);
    $display("MathFunc129 :: a : ", a, ", res : ", res);

    a = -1.0;
    res = $sinh(a);
    $display("MathFunc130 :: a : ", a, ", res : ", res);

    a = 10.0;
    res = $sinh(a);
    $display("MathFunc131 :: a : ", a, ", res : ", res);

    a = -10.0;
    res = $sinh(a);
    $display("MathFunc132 :: a : ", a, ", res : ", res);

    a = 700.0;
    res = $sinh(a);
    $display("MathFunc133 :: a : ", a, ", res : ", res);

    a = 710.0;
    res = $sinh(a);
    $display("MathFunc134 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $cosh(a);
    $display("MathFunc135 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $cosh(a);
    $display("MathFunc136 :: a : ", a, ", res : ", res);

    a = -1.0;
    res = $cosh(a);
    $display("MathFunc137 :: a : ", a, ", res : ", res);

    a = 10.0;
    res = $cosh(a);
    $display("MathFunc138 :: a : ", a, ", res : ", res);

    a = 700.0;
    res = $cosh(a);
    $display("MathFunc139 :: a : ", a, ", res : ", res);

    a = -700.0;
    res = $cosh(a);
    $display("MathFunc140 :: a : ", a, ", res : ", res);

    a = 710.0;
    res = $cosh(a);
    $display("MathFunc141 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $tanh(a);
    $display("MathFunc142 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $tanh(a);
    $display("MathFunc143 :: a : ", a, ", res : ", res);

    a = -1.0;
    res = $tanh(a);
    $display("MathFunc144 :: a : ", a, ", res : ", res);

    a = 10.0;
    res = $tanh(a);
    $display("MathFunc145 :: a : ", a, ", res : ", res);

    a = -10.0;
    res = $tanh(a);
    $display("MathFunc146 :: a : ", a, ", res : ", res);

    a = 700.0;
    res = $tanh(a);
    $display("MathFunc147 :: a : ", a, ", res : ", res);

    a = -700.0;
    res = $tanh(a);
    $display("MathFunc148 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $sin(a);
    $display("MathFunc149 :: a : ", a, ", res : ", res);

    a = 1.570796;
    res = $sin(a);
    $display("MathFunc150 :: a : ", a, ", res : ", res);

    a = -1.570796;
    res = $sin(a);
    $display("MathFunc151 :: a : ", a, ", res : ", res);

    a = 3.141593;
    res = $sin(a);
    $display("MathFunc152 :: a : ", a, ", res : ", res);

    a = 1e10;
    res = $sin(a);
    $display("MathFunc153 :: a : ", a, ", res : ", res);

    a = -1e10;
    res = $sin(a);
    $display("MathFunc154 :: a : ", a, ", res : ", res);

    a = 1e-10;
    res = $sin(a);
    $display("MathFunc155 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $asinh(a);
    $display("MathFunc156 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $asinh(a);
    $display("MathFunc157 :: a : ", a, ", res : ", res);

    a = -1.0;
    res = $asinh(a);
    $display("MathFunc158 :: a : ", a, ", res : ", res);

    a = 10.0;
    res = $asinh(a);
    $display("MathFunc159 :: a : ", a, ", res : ", res);

    a = -10.0;
    res = $asinh(a);
    $display("MathFunc160 :: a : ", a, ", res : ", res);

    a = 1e20;
    res = $asinh(a);
    $display("MathFunc161 :: a : ", a, ", res : ", res);

    a = 1e308;
    res = $asinh(a);
    $display("MathFunc162 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $cos(a);
    $display("MathFunc163 :: a : ", a, ", res : ", res);

    a = 1.570796;
    res = $cos(a);
    $display("MathFunc164 :: a : ", a, ", res : ", res);

    a = 3.141593;
    res = $cos(a);
    $display("MathFunc165 :: a : ", a, ", res : ", res);

    a = -3.141593;
    res = $cos(a);
    $display("MathFunc166 :: a : ", a, ", res : ", res);

    a = 1e10;
    res = $cos(a);
    $display("MathFunc167 :: a : ", a, ", res : ", res);

    a = -1e10;
    res = $cos(a);
    $display("MathFunc168 :: a : ", a, ", res : ", res);

    a = 1e-10;
    res = $cos(a);
    $display("MathFunc169 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $acosh(a);
    $display("MathFunc170 :: a : ", a, ", res : ", res);

    a = 2.0;
    res = $acosh(a);
    $display("MathFunc171 :: a : ", a, ", res : ", res);

    a = 10.0;
    res = $acosh(a);
    $display("MathFunc172 :: a : ", a, ", res : ", res);

    a = 1e10;
    res = $acosh(a);
    $display("MathFunc173 :: a : ", a, ", res : ", res);

    a = 1e308;
    res = $acosh(a);
    $display("MathFunc174 :: a : ", a, ", res : ", res);

    a = 0.5;
    res = $acosh(a);
    $display("MathFunc175 :: a : ", a, ", res : ", res);

    a = -10.0;
    res = $acosh(a);
    $display("MathFunc176 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $tan(a);
    $display("MathFunc177 :: a : ", a, ", res : ", res);

    a = 0.785398;
    res = $tan(a);
    $display("MathFunc178 :: a : ", a, ", res : ", res);

    a = -0.785398;
    res = $tan(a);
    $display("MathFunc179 :: a : ", a, ", res : ", res);
    
    a = 1.570796;
    res = $tan(a);
    $display("MathFunc180 :: a : ", a, ", res : ", res);

    a = -1.570796;
    res = $tan(a);
    $display("MathFunc181 :: a : ", a, ", res : ", res);

    a = 1e10;
    res = $tan(a);
    $display("MathFunc182 :: a : ", a, ", res : ", res);

    a = 1e-10;
    res = $tan(a);
    $display("MathFunc183 :: a : ", a, ", res : ", res);

    a = 0.0;
    res = $atanh(a);
    $display("MathFunc184 :: a : ", a, ", res : ", res);

    a = 0.5;
    res = $atanh(a);
    $display("MathFunc185 :: a : ", a, ", res : ", res);

    a = -0.5;
    res = $atanh(a);
    $display("MathFunc186 :: a : ", a, ", res : ", res);

    a = 0.99;
    res = $atanh(a);
    $display("MathFunc187 :: a : ", a, ", res : ", res);

    a = -0.99;
    res = $atanh(a);
    $display("MathFunc188 :: a : ", a, ", res : ", res);

    a = 1.0;
    res = $atanh(a);

    a = -1.0;
    res = $atanh(a);
    
  end

endmodule
