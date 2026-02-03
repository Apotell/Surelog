module tb;

  int i; real r; byte b; logic [31:0] l;

  initial begin
    // $itor
    i = -5; 
    r = $itor(i);

    // $rtoi
    r = 3.9;
    i = $rtoi(r);

    // $signed
    i = 127;   
    l = $signed(i);

    // $unsigned
    i = -1; 
    l = $unsigned(i);

    // $cast
    void'($cast(i, 3.9);

  end

endmodule

