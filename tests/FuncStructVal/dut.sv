
package cva6_config_pkg;

  localparam CVA6ConfigXlen = 32;

  localparam CVA6ConfigFpuEn = 0;
  localparam CVA6ConfigF16En = 0;
  localparam CVA6ConfigF16AltEn = 0;
  localparam CVA6ConfigF8En = 0;
  localparam CVA6ConfigFVecEn = 0;

  localparam CVA6ConfigCvxifEn = 0;
  localparam CVA6ConfigCExtEn = 1;
  localparam CVA6ConfigZcbExtEn = 0;
  localparam CVA6ConfigZcmpExtEn = 0;
  localparam CVA6ConfigAExtEn = 1;
  localparam CVA6ConfigBExtEn = 0;
  localparam CVA6ConfigVExtEn = 0;
  localparam CVA6ConfigZiCondExtEn = 0;

  localparam CVA6ConfigAxiIdWidth = 4;
  localparam CVA6ConfigAxiAddrWidth = 64;
  localparam CVA6ConfigAxiDataWidth = 64;
  localparam CVA6ConfigFetchUserEn = 0;
  localparam CVA6ConfigFetchUserWidth = CVA6ConfigXlen;
  localparam CVA6ConfigDataUserEn = 0;
  localparam CVA6ConfigDataUserWidth = CVA6ConfigXlen;

  localparam CVA6ConfigIcacheByteSize = 16384;
  localparam CVA6ConfigIcacheSetAssoc = 4;
  localparam CVA6ConfigIcacheLineWidth = 128;
  localparam CVA6ConfigDcacheByteSize = 32768;
  localparam CVA6ConfigDcacheSetAssoc = 8;
  localparam CVA6ConfigDcacheLineWidth = 128;

  localparam CVA6ConfigDcacheIdWidth = 1;
  localparam CVA6ConfigMemTidWidth = 2;

  localparam CVA6ConfigWtDcacheWbufDepth = 8;

  localparam CVA6ConfigNrCommitPorts = 2;
  localparam CVA6ConfigNrScoreboardEntries = 8;
  localparam CVA6ConfigNrLoadBufEntries = 2;

  localparam CVA6ConfigFPGAEn = 0;

  localparam CVA6ConfigNrLoadPipeRegs = 1;
  localparam CVA6ConfigNrStorePipeRegs = 0;

  localparam CVA6ConfigInstrTlbEntries = 2;
  localparam CVA6ConfigDataTlbEntries = 2;

  localparam CVA6ConfigRASDepth = 2;
  localparam CVA6ConfigBTBEntries = 32;
  localparam CVA6ConfigBHTEntries = 128;

  localparam CVA6ConfigTvalEn = 1;

  localparam CVA6ConfigNrPMPEntries = 8;

  localparam CVA6ConfigPerfCounterEn = 1;

  localparam config_pkg::cache_type_t CVA6ConfigDcacheType = config_pkg::WT;

  localparam CVA6ConfigMmuPresent = 1;

  localparam CVA6ConfigRvfiTrace = 1;

  localparam config_pkg::cva6_user_cfg_t cva6_cfg = '{
      XLEN: unsigned'(CVA6ConfigXlen),
      FPGA_EN: bit'(CVA6ConfigFPGAEn),
      NrCommitPorts: unsigned'(CVA6ConfigNrCommitPorts),
      AxiAddrWidth: unsigned'(CVA6ConfigAxiAddrWidth),
      AxiDataWidth: unsigned'(CVA6ConfigAxiDataWidth),
      AxiIdWidth: unsigned'(CVA6ConfigAxiIdWidth),
      AxiUserWidth: unsigned'(CVA6ConfigDataUserWidth),
      MemTidWidth: unsigned'(CVA6ConfigMemTidWidth),
      NrLoadBufEntries: unsigned'(CVA6ConfigNrLoadBufEntries),
      FpuEn: bit'(CVA6ConfigFpuEn),
      XF16: bit'(CVA6ConfigF16En),
      XF16ALT: bit'(CVA6ConfigF16AltEn),
      XF8: bit'(CVA6ConfigF8En),
      RVA: bit'(CVA6ConfigAExtEn),
      RVB: bit'(CVA6ConfigBExtEn),
      RVV: bit'(CVA6ConfigVExtEn),
      RVC: bit'(CVA6ConfigCExtEn),
      RVZCB: bit'(CVA6ConfigZcbExtEn),
      RVZCMP: bit'(CVA6ConfigZcmpExtEn),
      XFVec: bit'(CVA6ConfigFVecEn),
      CvxifEn: bit'(CVA6ConfigCvxifEn),
      ZiCondExtEn: bit'(CVA6ConfigZiCondExtEn),
      NrScoreboardEntries: unsigned'(CVA6ConfigNrScoreboardEntries),
      RVS: bit'(1),
      RVU: bit'(1),
      HaltAddress: 64'h800,
      ExceptionAddress: 64'h808,
      RASDepth: unsigned'(CVA6ConfigRASDepth),
      BTBEntries: unsigned'(CVA6ConfigBTBEntries),
      BHTEntries: unsigned'(CVA6ConfigBHTEntries),
      DmBaseAddress: 64'h0,
      TvalEn: unsigned'(CVA6ConfigTvalEn),
      NrPMPEntries: unsigned'(CVA6ConfigNrPMPEntries),
      PMPCfgRstVal: {16{64'h0}},
      PMPAddrRstVal: {16{64'h0}},
      PMPEntryReadOnly: 16'd0,
      NOCType: config_pkg::NOC_TYPE_AXI4_ATOP,
      NrNonIdempotentRules: unsigned'(2),
      NonIdempotentAddrBase: 1024'({64'b0, 64'b0}),
      NonIdempotentLength: 1024'({64'b0, 64'b0}),
      NrExecuteRegionRules: unsigned'(3),
      //                      DRAM,          Boot ROM,   Debug Module
      ExecuteRegionAddrBase:
      1024'(
      {64'h8000_0000, 64'h1_0000, 64'h0}
      ),
      ExecuteRegionLength: 1024'({64'h40000000, 64'h10000, 64'h1000}),
      NrCachedRegionRules: unsigned'(1),
      CachedRegionAddrBase: 1024'({64'h8000_0000}),
      CachedRegionLength: 1024'({64'h40000000}),
      MaxOutstandingStores: unsigned'(7),
      DebugEn: bit'(1),
      AxiBurstWriteEn: bit'(0),
      IcacheByteSize: unsigned'(CVA6ConfigIcacheByteSize),
      IcacheSetAssoc: unsigned'(CVA6ConfigIcacheSetAssoc),
      IcacheLineWidth: unsigned'(CVA6ConfigIcacheLineWidth),
      DcacheByteSize: unsigned'(CVA6ConfigDcacheByteSize),
      DcacheSetAssoc: unsigned'(CVA6ConfigDcacheSetAssoc),
      DcacheLineWidth: unsigned'(CVA6ConfigDcacheLineWidth),
      DataUserEn: unsigned'(CVA6ConfigDataUserEn),
      FetchUserWidth: unsigned'(CVA6ConfigFetchUserWidth),
      FetchUserEn: unsigned'(CVA6ConfigFetchUserEn)
  };
endpackage




package config_pkg;

  // ---------------
  // Global Config
  // ---------------
  localparam int unsigned ILEN = 32;
  localparam int unsigned NRET = 1;

  /// The NoC type is a top-level parameter, hence we need a bit more
  /// information on what protocol those type parameters are supporting.
  /// Currently two values are supported"
  typedef enum {
    /// The "classic" AXI4 protocol.
    NOC_TYPE_AXI4_ATOP,
    /// In the OpenPiton setting the WT cache is connected to the L15.
    NOC_TYPE_L15_BIG_ENDIAN,
    NOC_TYPE_L15_LITTLE_ENDIAN
  } noc_type_e;

  /// Cache type parameter
  typedef enum logic [1:0] {
    WB = 0,
    WT = 1,
    HPDCACHE = 2
  } cache_type_t;

  /// Data and Address length
  typedef enum logic [3:0] {
    ModeOff  = 0,
    ModeSv32 = 1,
    ModeSv39 = 8,
    ModeSv48 = 9,
    ModeSv57 = 10,
    ModeSv64 = 11
  } vm_mode_t;

  localparam NrMaxRules = 16;

  typedef struct packed {
    // General Purpose Register Size (in bits)
    int unsigned                 XLEN;
    // Is FPGA optimization of CV32A6
    bit                          FPGA_EN;
    // Number of commit ports
    int unsigned                 NrCommitPorts;
    // AXI address width
    int unsigned                 AxiAddrWidth;
    // AXI data width
    int unsigned                 AxiDataWidth;
    // AXI ID width
    int unsigned                 AxiIdWidth;
    // AXI User width
    int unsigned                 AxiUserWidth;
    // TODO
    int unsigned                 MemTidWidth;
    // Load buffer entry buffer
    int unsigned                 NrLoadBufEntries;
    // Floating Point
    bit                          FpuEn;
    // Non standard 16bits Floating Point
    bit                          XF16;
    // Non standard 16bits Floating Point Alt
    bit                          XF16ALT;
    // Non standard 8bits Floating Point
    bit                          XF8;
    // Atomic RISC-V extension
    bit                          RVA;
    // Bit manipulation RISC-V extension
    bit                          RVB;
    // Vector RISC-V extension
    bit                          RVV;
    // Compress RISC-V extension
    bit                          RVC;
    // Zcb RISC-V extension
    bit                          RVZCB;
    // Zcmp RISC-V extension
    bit                          RVZCMP;
    // Non standard Vector Floating Point
    bit                          XFVec;
    // CV-X-IF coprocessor interface is supported
    bit                          CvxifEn;
    // Zicond RISC-V extension
    bit                          ZiCondExtEn;
    // Supervisor mode
    bit                          RVS;
    // User mode
    bit                          RVU;
    // Scoreboard length
    int unsigned                 NrScoreboardEntries;
    // Address to jump when halt request
    logic [63:0]                 HaltAddress;
    // Address to jump when exception 
    logic [63:0]                 ExceptionAddress;
    // Return address stack depth
    int unsigned                 RASDepth;
    // Branch target buffer entries
    int unsigned                 BTBEntries;
    // Branch history entries
    int unsigned                 BHTEntries;
    // Base address of the debug module
    logic [63:0]                 DmBaseAddress;
    // Tval Support Enable
    bit                          TvalEn;
    // Number of PMP entries
    int unsigned                 NrPMPEntries;
    // PMP CSR configuration reset values
    logic [15:0][63:0]           PMPCfgRstVal;
    // PMP CSR address reset values
    logic [15:0][63:0]           PMPAddrRstVal;
    // PMP CSR read-only bits
    bit [15:0]                   PMPEntryReadOnly;
    // NOC bus type
    noc_type_e                   NOCType;
    // Number of PMA non idempotent rules
    int unsigned                 NrNonIdempotentRules;
    // PMA NonIdempotent region base address
    logic [NrMaxRules-1:0][63:0] NonIdempotentAddrBase;
    // PMA NonIdempotent region length
    logic [NrMaxRules-1:0][63:0] NonIdempotentLength;
    // Number of PMA regions with execute rules
    int unsigned                 NrExecuteRegionRules;
    // PMA Execute region base address
    logic [NrMaxRules-1:0][63:0] ExecuteRegionAddrBase;
    // PMA Execute region address base
    logic [NrMaxRules-1:0][63:0] ExecuteRegionLength;
    // Number of PMA regions with cache rules
    int unsigned                 NrCachedRegionRules;
    // PMA cache region base address
    logic [NrMaxRules-1:0][63:0] CachedRegionAddrBase;
    // PMA cache region rules
    logic [NrMaxRules-1:0][63:0] CachedRegionLength;
    // Maximum number of outstanding stores
    int unsigned                 MaxOutstandingStores;
    // Debug support
    bit                          DebugEn;
    // AXI burst in write
    bit                          AxiBurstWriteEn;
    // Instruction cache size (in bytes)
    int unsigned                 IcacheByteSize;
    // Instruction cache associativity (number of ways)
    int unsigned                 IcacheSetAssoc;
    // Instruction line width
    int unsigned                 IcacheLineWidth;
    // Data cache size (in bytes)
    int unsigned                 DcacheByteSize;
    // Data cache associativity (number of ways)
    int unsigned                 DcacheSetAssoc;
    // Data line width
    int unsigned                 DcacheLineWidth;
    // TODO
    int unsigned                 DataUserEn;
    // TODO
    int unsigned                 FetchUserWidth;
    // TODO
    int unsigned                 FetchUserEn;
  } cva6_user_cfg_t;

  typedef struct packed {
    int unsigned XLEN;
    int unsigned VLEN;
    int unsigned PLEN;
    bit IS_XLEN32;
    bit IS_XLEN64;
    int unsigned XLEN_ALIGN_BYTES;
    int unsigned ASID_WIDTH;

    bit          FPGA_EN;
    /// Number of commit ports, i.e., maximum number of instructions that the
    /// core can retire per cycle. It can be beneficial to have more commit
    /// ports than issue ports, for the scoreboard to empty out in case one
    /// instruction stalls a little longer.
    int unsigned NrCommitPorts;
    /// AXI parameters.
    int unsigned AxiAddrWidth;
    int unsigned AxiDataWidth;
    int unsigned AxiIdWidth;
    int unsigned AxiUserWidth;
    int unsigned MEM_TID_WIDTH;
    int unsigned NrLoadBufEntries;
    bit          FpuEn;
    bit          XF16;
    bit          XF16ALT;
    bit          XF8;
    bit          RVA;
    bit          RVB;
    bit          RVV;
    bit          RVC;
    bit          RVZCB;
    bit          RVZCMP;
    bit          XFVec;
    bit          CvxifEn;
    bit          ZiCondExtEn;

    int unsigned NR_SB_ENTRIES;
    int unsigned TRANS_ID_BITS;

    bit          RVF;
    bit          RVD;
    bit          FpPresent;
    bit          NSX;
    int unsigned FLen;
    bit          RVFVec;
    bit          XF16Vec;
    bit          XF16ALTVec;
    bit          XF8Vec;
    int unsigned NrRgprPorts;
    int unsigned NrWbPorts;
    bit          EnableAccelerator;
    bit          RVS;                //Supervisor mode
    bit          RVU;                //User mode

    logic [63:0]                 HaltAddress;
    logic [63:0]                 ExceptionAddress;
    int unsigned                 RASDepth;
    int unsigned                 BTBEntries;
    int unsigned                 BHTEntries;
    logic [63:0]                 DmBaseAddress;
    bit                          TvalEn;
    int unsigned                 NrPMPEntries;
    logic [15:0][63:0]           PMPCfgRstVal;
    logic [15:0][63:0]           PMPAddrRstVal;
    bit [15:0]                   PMPEntryReadOnly;
    noc_type_e                   NOCType;
    int unsigned                 NrNonIdempotentRules;
    logic [NrMaxRules-1:0][63:0] NonIdempotentAddrBase;
    logic [NrMaxRules-1:0][63:0] NonIdempotentLength;
    int unsigned                 NrExecuteRegionRules;
    logic [NrMaxRules-1:0][63:0] ExecuteRegionAddrBase;
    logic [NrMaxRules-1:0][63:0] ExecuteRegionLength;
    int unsigned                 NrCachedRegionRules;
    logic [NrMaxRules-1:0][63:0] CachedRegionAddrBase;
    logic [NrMaxRules-1:0][63:0] CachedRegionLength;
    int unsigned                 MaxOutstandingStores;
    bit                          DebugEn;
    bit                          NonIdemPotenceEn;       // Currently only used by V extension (Ara)
    bit                          AxiBurstWriteEn;

    int unsigned ICACHE_SET_ASSOC;
    int unsigned ICACHE_SET_ASSOC_WIDTH;
    int unsigned ICACHE_INDEX_WIDTH;
    int unsigned ICACHE_TAG_WIDTH;
    int unsigned ICACHE_LINE_WIDTH;
    int unsigned ICACHE_USER_LINE_WIDTH;
    int unsigned DCACHE_SET_ASSOC;
    int unsigned DCACHE_SET_ASSOC_WIDTH;
    int unsigned DCACHE_INDEX_WIDTH;
    int unsigned DCACHE_TAG_WIDTH;
    int unsigned DCACHE_LINE_WIDTH;
    int unsigned DCACHE_USER_LINE_WIDTH;
    int unsigned DCACHE_USER_WIDTH;
    int unsigned DCACHE_OFFSET_WIDTH;
    int unsigned DCACHE_NUM_WORDS;

    int unsigned DCACHE_MAX_TX;

    int unsigned DATA_USER_EN;
    int unsigned FETCH_USER_WIDTH;
    int unsigned FETCH_USER_EN;
    int unsigned AXI_USER_EN;

    int unsigned FETCH_WIDTH;
    int unsigned INSTR_PER_FETCH;
    int unsigned LOG2_INSTR_PER_FETCH;

    int unsigned ModeW;
    int unsigned ASIDW;
    int unsigned PPNW;
    vm_mode_t MODE_SV;
    int unsigned SV;
  } cva6_cfg_t;

  /// Empty configuration to sanity check proper parameter passing. Whenever
  /// you develop a module that resides within the core, assign this constant.
  localparam cva6_cfg_t cva6_cfg_empty = '0;

  /// Utility function being called to check parameters. Not all values make
  /// sense for all parameters, here is the place to sanity check them.
  function automatic void check_cfg(cva6_cfg_t Cfg);
    // pragma translate_off
`ifndef VERILATOR
    assert (Cfg.RASDepth > 0);
    assert (2 ** $clog2(Cfg.BTBEntries) == Cfg.BTBEntries);
    assert (2 ** $clog2(Cfg.BHTEntries) == Cfg.BHTEntries);
    assert (Cfg.NrNonIdempotentRules <= NrMaxRules);
    assert (Cfg.NrExecuteRegionRules <= NrMaxRules);
    assert (Cfg.NrCachedRegionRules <= NrMaxRules);
    assert (Cfg.NrPMPEntries <= 16);
`endif
    // pragma translate_on
  endfunction

  function automatic logic range_check(logic [63:0] base, logic [63:0] len, logic [63:0] address);
    // if len is a power of two, and base is properly aligned, this check could be simplified
    // Extend base by one bit to prevent an overflow.
    return (address >= base) && (({1'b0, address}) < (65'(base) + len));
  endfunction : range_check


  function automatic logic is_inside_nonidempotent_regions(cva6_cfg_t Cfg, logic [63:0] address);
    logic [NrMaxRules-1:0] pass;
    pass = '0;
    for (int unsigned k = 0; k < Cfg.NrNonIdempotentRules; k++) begin
      pass[k] = range_check(Cfg.NonIdempotentAddrBase[k], Cfg.NonIdempotentLength[k], address);
    end
    return |pass;
  endfunction : is_inside_nonidempotent_regions

  function automatic logic is_inside_execute_regions(cva6_cfg_t Cfg, logic [63:0] address);
    // if we don't specify any region we assume everything is accessible
    logic [NrMaxRules-1:0] pass;
    pass = '0;
    for (int unsigned k = 0; k < Cfg.NrExecuteRegionRules; k++) begin
      pass[k] = range_check(Cfg.ExecuteRegionAddrBase[k], Cfg.ExecuteRegionLength[k], address);
    end
    return |pass;
  endfunction : is_inside_execute_regions

  function automatic logic is_inside_cacheable_regions(cva6_cfg_t Cfg, logic [63:0] address);
    automatic logic [NrMaxRules-1:0] pass;
    pass = '0;
    for (int unsigned k = 0; k < Cfg.NrCachedRegionRules; k++) begin
      pass[k] = range_check(Cfg.CachedRegionAddrBase[k], Cfg.CachedRegionLength[k], address);
    end
    return |pass;
  endfunction : is_inside_cacheable_regions

endpackage

package build_config_pkg;

  function automatic config_pkg::cva6_cfg_t build_config(config_pkg::cva6_user_cfg_t CVA6Cfg);
  
    bit IS_XLEN32 = (CVA6Cfg.XLEN == 32) ? 1'b1 : 1'b0;
    bit IS_XLEN64 = (CVA6Cfg.XLEN == 32) ? 1'b0 : 1'b1;
    bit RVF = (IS_XLEN64 | IS_XLEN32) & CVA6Cfg.FpuEn;
    bit RVD = (IS_XLEN64 ? 1 : 0) & CVA6Cfg.FpuEn;
    bit FpPresent = RVF | RVD | CVA6Cfg.XF16 | CVA6Cfg.XF16ALT | CVA6Cfg.XF8;
    bit NSX = CVA6Cfg.XF16 | CVA6Cfg.XF16ALT | CVA6Cfg.XF8 | CVA6Cfg.XFVec;  // Are non-standard extensions present?
    int unsigned FLen = RVD ? 64 :  // D ext.
    RVF ? 32 :  // F ext.
    CVA6Cfg.XF16 ? 16 :  // Xf16 ext.
    CVA6Cfg.XF16ALT ? 16 :  // Xf16alt ext.
    CVA6Cfg.XF8 ? 8 :  // Xf8 ext.
    1;  // Unused in case of no FP

    // Transprecision floating-point extensions configuration
    bit RVFVec     = RVF             & CVA6Cfg.XFVec & FLen>32; // FP32 vectors available if vectors and larger fmt enabled
    bit XF16Vec    = CVA6Cfg.XF16    & CVA6Cfg.XFVec & FLen>16; // FP16 vectors available if vectors and larger fmt enabled
    bit XF16ALTVec = CVA6Cfg.XF16ALT & CVA6Cfg.XFVec & FLen>16; // FP16ALT vectors available if vectors and larger fmt enabled
    bit XF8Vec     = CVA6Cfg.XF8     & CVA6Cfg.XFVec & FLen>8;  // FP8 vectors available if vectors and larger fmt enabled

    bit EnableAccelerator = CVA6Cfg.RVV;  // Currently only used by V extension (Ara)
    int unsigned NrWbPorts = (CVA6Cfg.CvxifEn || EnableAccelerator) ? 5 : 4;

    int unsigned ICACHE_INDEX_WIDTH = $clog2(CVA6Cfg.IcacheByteSize / CVA6Cfg.IcacheSetAssoc);
    int unsigned DCACHE_INDEX_WIDTH = $clog2(CVA6Cfg.DcacheByteSize / CVA6Cfg.DcacheSetAssoc);
    int unsigned DCACHE_OFFSET_WIDTH = $clog2(CVA6Cfg.DcacheLineWidth / 8);

    config_pkg::cva6_cfg_t cfg;

    cfg.XLEN = CVA6Cfg.XLEN;
    cfg.VLEN = (CVA6Cfg.XLEN == 32) ? 32 : 64;
    cfg.PLEN = (CVA6Cfg.XLEN == 32) ? 34 : 56;
    cfg.IS_XLEN32 = IS_XLEN32;
    cfg.IS_XLEN64 = IS_XLEN64;
    cfg.XLEN_ALIGN_BYTES = $clog2(CVA6Cfg.XLEN / 8);
    cfg.ASID_WIDTH = (CVA6Cfg.XLEN == 64) ? 16 : 1;

    cfg.FPGA_EN = CVA6Cfg.FPGA_EN;
    cfg.NrCommitPorts = CVA6Cfg.NrCommitPorts;
    cfg.AxiAddrWidth = CVA6Cfg.AxiAddrWidth;
    cfg.AxiDataWidth = CVA6Cfg.AxiDataWidth;
    cfg.AxiIdWidth = CVA6Cfg.AxiIdWidth;
    cfg.AxiUserWidth = CVA6Cfg.AxiUserWidth;
    cfg.MEM_TID_WIDTH = CVA6Cfg.MemTidWidth;
    cfg.NrLoadBufEntries = CVA6Cfg.NrLoadBufEntries;
    cfg.FpuEn = CVA6Cfg.FpuEn;
    cfg.XF16 = CVA6Cfg.XF16;
    cfg.XF16ALT = CVA6Cfg.XF16ALT;
    cfg.XF8 = CVA6Cfg.XF8;
    cfg.RVA = CVA6Cfg.RVA;
    cfg.RVB = CVA6Cfg.RVB;
    cfg.RVV = CVA6Cfg.RVV;
    cfg.RVC = CVA6Cfg.RVC;
    cfg.RVZCB = CVA6Cfg.RVZCB;
    cfg.RVZCMP = CVA6Cfg.RVZCMP;
    cfg.XFVec = CVA6Cfg.XFVec;
    cfg.CvxifEn = CVA6Cfg.CvxifEn;
    cfg.ZiCondExtEn = CVA6Cfg.ZiCondExtEn;
    cfg.NR_SB_ENTRIES = CVA6Cfg.NrScoreboardEntries;
    cfg.TRANS_ID_BITS = $clog2(CVA6Cfg.NrScoreboardEntries);

    cfg.RVF = bit'(RVF);
    cfg.RVD = bit'(RVD);
    cfg.FpPresent = bit'(FpPresent);
    cfg.NSX = bit'(NSX);
    cfg.FLen = unsigned'(FLen);
    cfg.RVFVec = bit'(RVFVec);
    cfg.XF16Vec = bit'(XF16Vec);
    cfg.XF16ALTVec = bit'(XF16ALTVec);
    cfg.XF8Vec = bit'(XF8Vec);
    cfg.NrRgprPorts = unsigned'(2);
    cfg.NrWbPorts = unsigned'(NrWbPorts);
    cfg.EnableAccelerator = bit'(EnableAccelerator);
    cfg.RVS = CVA6Cfg.RVS;
    cfg.RVU = CVA6Cfg.RVU;

    cfg.HaltAddress = CVA6Cfg.HaltAddress;
    cfg.ExceptionAddress = CVA6Cfg.ExceptionAddress;
    cfg.RASDepth = CVA6Cfg.RASDepth;
    cfg.BTBEntries = CVA6Cfg.BTBEntries;
    cfg.BHTEntries = CVA6Cfg.BHTEntries;
    cfg.DmBaseAddress = CVA6Cfg.DmBaseAddress;
    cfg.TvalEn = CVA6Cfg.TvalEn;
    cfg.NrPMPEntries = CVA6Cfg.NrPMPEntries;
    cfg.PMPCfgRstVal = CVA6Cfg.PMPCfgRstVal;
    cfg.PMPAddrRstVal = CVA6Cfg.PMPAddrRstVal;
    cfg.PMPEntryReadOnly = CVA6Cfg.PMPEntryReadOnly;
    cfg.NOCType = CVA6Cfg.NOCType;
    cfg.NrNonIdempotentRules = CVA6Cfg.NrNonIdempotentRules;
    cfg.NonIdempotentAddrBase = CVA6Cfg.NonIdempotentAddrBase;
    cfg.NonIdempotentLength = CVA6Cfg.NonIdempotentLength;
    cfg.NrExecuteRegionRules = CVA6Cfg.NrExecuteRegionRules;
    cfg.ExecuteRegionAddrBase = CVA6Cfg.ExecuteRegionAddrBase;
    cfg.ExecuteRegionLength = CVA6Cfg.ExecuteRegionLength;
    cfg.NrCachedRegionRules = CVA6Cfg.NrCachedRegionRules;
    cfg.CachedRegionAddrBase = CVA6Cfg.CachedRegionAddrBase;
    cfg.CachedRegionLength = CVA6Cfg.CachedRegionLength;
    cfg.MaxOutstandingStores = CVA6Cfg.MaxOutstandingStores;
    cfg.DebugEn = CVA6Cfg.DebugEn;
    cfg.NonIdemPotenceEn = CVA6Cfg.NrNonIdempotentRules && CVA6Cfg.NonIdempotentLength;
    cfg.AxiBurstWriteEn = CVA6Cfg.AxiBurstWriteEn;

    cfg.ICACHE_SET_ASSOC = CVA6Cfg.IcacheSetAssoc;
    cfg.ICACHE_SET_ASSOC_WIDTH = $clog2(CVA6Cfg.IcacheSetAssoc);
    cfg.ICACHE_INDEX_WIDTH = ICACHE_INDEX_WIDTH;
    cfg.ICACHE_TAG_WIDTH = cfg.PLEN - ICACHE_INDEX_WIDTH;
    cfg.ICACHE_LINE_WIDTH = CVA6Cfg.IcacheLineWidth;
    cfg.ICACHE_USER_LINE_WIDTH = (CVA6Cfg.AxiUserWidth == 1) ? 4 : CVA6Cfg.IcacheLineWidth;
    cfg.DCACHE_SET_ASSOC = CVA6Cfg.DcacheSetAssoc;
    cfg.DCACHE_SET_ASSOC_WIDTH = $clog2(CVA6Cfg.DcacheSetAssoc);
    cfg.DCACHE_INDEX_WIDTH = DCACHE_INDEX_WIDTH;
    cfg.DCACHE_TAG_WIDTH = cfg.PLEN - DCACHE_INDEX_WIDTH;
    cfg.DCACHE_LINE_WIDTH = CVA6Cfg.DcacheLineWidth;
    cfg.DCACHE_USER_LINE_WIDTH = (CVA6Cfg.AxiUserWidth == 1) ? 4 : CVA6Cfg.DcacheLineWidth;
    cfg.DCACHE_USER_WIDTH = CVA6Cfg.AxiUserWidth;
    cfg.DCACHE_OFFSET_WIDTH = DCACHE_OFFSET_WIDTH;
    cfg.DCACHE_NUM_WORDS = 2 ** (DCACHE_INDEX_WIDTH - DCACHE_OFFSET_WIDTH);

    cfg.DCACHE_MAX_TX = unsigned'(2 ** CVA6Cfg.MemTidWidth);

    cfg.DATA_USER_EN = CVA6Cfg.DataUserEn;
    cfg.FETCH_USER_WIDTH = CVA6Cfg.FetchUserWidth;
    cfg.FETCH_USER_EN = CVA6Cfg.FetchUserEn;
    cfg.AXI_USER_EN = CVA6Cfg.DataUserEn | CVA6Cfg.FetchUserEn;

    cfg.FETCH_WIDTH = 32;
    cfg.INSTR_PER_FETCH = CVA6Cfg.RVC == 1'b1 ? (cfg.FETCH_WIDTH / 16) : 1;
    
    cfg.LOG2_INSTR_PER_FETCH = CVA6Cfg.RVC == 1'b1 ? $clog2(cfg.INSTR_PER_FETCH) : 1;

    cfg.ModeW = (CVA6Cfg.XLEN == 32) ? 1 : 4;
    cfg.ASIDW = (CVA6Cfg.XLEN == 32) ? 9 : 16;
    cfg.PPNW = (CVA6Cfg.XLEN == 32) ? 22 : 44;
    cfg.MODE_SV = (CVA6Cfg.XLEN == 32) ? config_pkg::ModeSv32 : config_pkg::ModeSv39;
    cfg.SV = (cfg.MODE_SV == config_pkg::ModeSv32) ? 32 : 39;

    return cfg;
  endfunction

endpackage


module top();
 //parameter config_pkg::cva6_cfg_t CVA6Cfg = config_pkg::cva6_cfg_empty;

 parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg);

 popcount #(
        .INPUT_WIDTH(CVA6Cfg.INSTR_PER_FETCH)
    ) u1();

endmodule

module popcount #(
    parameter int unsigned INPUT_WIDTH = 2,
    localparam int unsigned PopcountWidth = $clog2(INPUT_WIDTH)+1
) (
    input logic [INPUT_WIDTH-1:0]     data_i,
    output logic [PopcountWidth-1:0] popcount_o
);

   localparam int unsigned PaddedWidth = 1 << $clog2(INPUT_WIDTH);

   logic [PaddedWidth-1:0]           padded_input;
   logic [PopcountWidth-2:0]         left_child_result, right_child_result;

   //Zero pad the input to next power of two
   always_comb begin
     padded_input = '0;
     padded_input[INPUT_WIDTH-1:0] = data_i;
   end

   //Recursive instantiation to build binary adder tree
   if (INPUT_WIDTH == 1) begin : single_node
     assign left_child_result  = 1'b0;
     assign right_child_result = padded_input[0];
   end else if (INPUT_WIDTH == 2) begin : leaf_node
     assign left_child_result  = padded_input[1];
     assign right_child_result = padded_input[0];
   end else begin : non_leaf_node
     popcount #(.INPUT_WIDTH(PaddedWidth / 2))
         left_child(
                    .data_i(padded_input[PaddedWidth-1:PaddedWidth/2]),
                    .popcount_o(left_child_result));

     popcount #(.INPUT_WIDTH(PaddedWidth / 2))
         right_child(
                     .data_i(padded_input[PaddedWidth/2-1:0]),
                     .popcount_o(right_child_result));
   end

   //Output assignment
   assign popcount_o = left_child_result + right_child_result;

endmodule : popcount

