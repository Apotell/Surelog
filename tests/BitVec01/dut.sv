module tb();

    int count;
    bit result;
    logic [7:0] val;

    initial begin
        val = 8'b1100_x0z1;

        // $countbits: Counts bits matching specific values (e.g., count all 1s)
        count = $countbits(val, 1'b1);
        
        // $countones: Counts only bits that are 1 (ignores x and z)
        count = $countones(val);
        
        // $onehot: Returns true if exactly one bit is 1
        result = $onehot(4'b0010);
        
        // $onehot0: Returns true if at most one bit is 1 (zero or one)
        result = $onehot0(4'b0000);
        
        // $isunknown: Returns true if any bit is X or Z
        result = $isunknown(val);
    end

endmodule
	
	

