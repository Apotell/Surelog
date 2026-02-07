module tb;
  int i; real r; logic [31:0] l;

  initial begin
    // $itor
    r = $itor(-5);

    // $rtoi
    i = $rtoi(3.9);

    // $signed
    l = $signed(127);

    // $unsigned
    l = $unsigned(-1);

    // $cast
    void'($cast(i, 3.9));
  end
endmodule
