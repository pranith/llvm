//===- MipsMSAInstrFormats.td - Mips Instruction Formats ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def HasMSA : Predicate<"Subtarget.hasMSA()">,
             AssemblerPredicate<"FeatureMSA">;

class MSAInst : MipsInst<(outs), (ins), "", [], NoItinerary, FrmOther> {
  let Predicates = [HasMSA];
  let Inst{31-26} = 0b011110;
}

class PseudoMSA<dag outs, dag ins, list<dag> pattern,
                InstrItinClass itin = IIPseudo>:
  MipsPseudo<outs, ins, pattern, itin> {
  let Predicates = [HasMSA];
}

class MSA_BIT_B_FMT<bits<3> major, bits<6> minor>: MSAInst {
  let Inst{25-23} = major;
  let Inst{22-19} = 0b1110;
  let Inst{5-0} = minor;
}

class MSA_BIT_H_FMT<bits<3> major, bits<6> minor>: MSAInst {
  let Inst{25-23} = major;
  let Inst{22-20} = 0b110;
  let Inst{5-0} = minor;
}

class MSA_BIT_W_FMT<bits<3> major, bits<6> minor>: MSAInst {
  let Inst{25-23} = major;
  let Inst{22-21} = 0b10;
  let Inst{5-0} = minor;
}

class MSA_BIT_D_FMT<bits<3> major, bits<6> minor>: MSAInst {
  let Inst{25-23} = major;
  let Inst{22} = 0b0;
  let Inst{5-0} = minor;
}

class MSA_2R_FMT<bits<8> major, bits<2> df, bits<6> minor>: MSAInst {
  let Inst{25-18} = major;
  let Inst{17-16} = df;
  let Inst{5-0} = minor;
}

class MSA_2RF_FMT<bits<9> major, bits<1> df, bits<6> minor>: MSAInst {
  let Inst{25-17} = major;
  let Inst{16} = df;
  let Inst{5-0} = minor;
}

class MSA_3R_FMT<bits<3> major, bits<2> df, bits<6> minor>: MSAInst {
  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{5-0} = minor;
}

class MSA_3RF_FMT<bits<4> major, bits<1> df, bits<6> minor>: MSAInst {
  let Inst{25-22} = major;
  let Inst{21} = df;
  let Inst{5-0} = minor;
}

class MSA_ELM_B_FMT<bits<4> major, bits<6> minor>: MSAInst {
  let Inst{25-22} = major;
  let Inst{21-20} = 0b00;
  let Inst{5-0} = minor;
}

class MSA_ELM_H_FMT<bits<4> major, bits<6> minor>: MSAInst {
  let Inst{25-22} = major;
  let Inst{21-19} = 0b100;
  let Inst{5-0} = minor;
}

class MSA_ELM_W_FMT<bits<4> major, bits<6> minor>: MSAInst {
  let Inst{25-22} = major;
  let Inst{21-18} = 0b1100;
  let Inst{5-0} = minor;
}

class MSA_ELM_D_FMT<bits<4> major, bits<6> minor>: MSAInst {
  let Inst{25-22} = major;
  let Inst{21-17} = 0b11100;
  let Inst{5-0} = minor;
}

class MSA_I5_FMT<bits<3> major, bits<2> df, bits<6> minor>: MSAInst {
  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{5-0} = minor;
}

class MSA_I8_FMT<bits<2> major, bits<6> minor>: MSAInst {
  let Inst{25-24} = major;
  let Inst{5-0} = minor;
}

class MSA_I10_FMT<bits<3> major, bits<2> df, bits<6> minor>: MSAInst {
  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{5-0} = minor;
}

class MSA_VEC_FMT<bits<5> major, bits<6> minor>: MSAInst {
  let Inst{25-21} = major;
  let Inst{5-0} = minor;
}
