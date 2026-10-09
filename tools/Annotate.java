// Ghidra headless script: apply names / signatures / jump-table overrides from a
// text file, then decompile a code range.
//
// Args: <annotations.txt> <out.c> <lo-hex> <hi-hex>
//
// Annotation commands (one per line, '#' comments):
//   fn    ADDR NAME                 create (if needed) + rename function at 1000:ADDR
//   sig   ADDR C-prototype          apply prototype (calling conv defaults to __cdecl16near)
//   cc    ADDR CONVENTION
//   purge ADDR N
//   reg   ADDR RET PARAM...         custom register storage, e.g. reg 7c63 DX:AX DX:AX CX:BX
//   jt    JMPADDR T1 T2 ...         jump table override (targets in code segment)
//   data  OFF NAME [TYPE [COUNT]]   label DS:OFF (TYPE: byte word dword char), COUNT -> array
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.JumpTable;
import ghidra.program.model.symbol.SourceType;
import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import java.io.*;
import java.util.*;

public class Annotate extends GhidraScript {
    AddressSpace sp;
    Address code(String hex) { return toAddr("1000:" + hex); }
    Address ds(String hex) { return toAddr("17ec:" + hex); }

    Function fn(Address a) throws Exception {
        Function f = getFunctionAt(a);
        if (f == null) {
            disassemble(a);
            f = createFunction(a, null);
        }
        return f;
    }

    VariableStorage regs(String spec) throws Exception {
        String[] parts = spec.split(":");
        Register[] r = new Register[parts.length];
        for (int i = 0; i < parts.length; i++) r[i] = currentProgram.getRegister(parts[i]);
        return new VariableStorage(currentProgram, r);
    }

    DataType dt(String t) {
        switch (t) {
            case "byte": return ByteDataType.dataType;
            case "char": return CharDataType.dataType;
            case "word": return WordDataType.dataType;
            case "int": return ShortDataType.dataType;
            case "dword": return DWordDataType.dataType;
            case "long": return SignedDWordDataType.dataType;
        }
        return null;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        BufferedReader br = new BufferedReader(new FileReader(a[0]));
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] t = line.split("\\s+", 3);
            try {
                switch (t[0]) {
                    case "fn": {
                        Function f = fn(code(t[1]));
                        f.setName(t[2], SourceType.USER_DEFINED);
                        break;
                    }
                    case "sig": {
                        Function f = fn(code(t[1]));
                        FunctionDefinitionDataType def = CParserUtils.parseSignature((ghidra.app.services.DataTypeManagerService) null, currentProgram, t[2]);
                        if (def == null) { println("bad sig: " + line); break; }
                        new ApplyFunctionSignatureCmd(f.getEntryPoint(), def, SourceType.USER_DEFINED).applyTo(currentProgram, monitor);
                        if (f.getCallingConventionName() == null || f.getCallingConventionName().equals("unknown"))
                            f.setCallingConvention("__cdecl16near");
                        break;
                    }
                    case "cc": fn(code(t[1])).setCallingConvention(t[2]); break;
                    case "purge": fn(code(t[1])).setStackPurgeSize(Integer.parseInt(t[2])); break;
                    case "reg": {
                        Function f = fn(code(t[1]));
                        String[] r = t[2].split("\\s+");
                        DataType lng = SignedDWordDataType.dataType; // 4 bytes
                        ReturnParameterImpl ret = new ReturnParameterImpl(lng, regs(r[0]), currentProgram);
                        List<Variable> ps = new ArrayList<>();
                        for (int i = 1; i < r.length; i++) {
                            VariableStorage st = regs(r[i]);
                            DataType pt = st.size() == 4 ? lng : (st.size() == 1 ? ByteDataType.dataType : ShortDataType.dataType);
                            ps.add(new ParameterImpl("p" + i, pt, st, currentProgram));
                        }
                        f.updateFunction(f.getCallingConventionName(), ret, ps,
                            Function.FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
                        break;
                    }
                    case "jt": {
                        String[] r = (t[1] + " " + t[2]).split("\\s+");
                        Address j = code(r[0]);
                        ArrayList<Address> dl = new ArrayList<>();
                        for (int i = 1; i < r.length; i++) {
                            Address d = code(r[i]);
                            disassemble(d);
                            currentProgram.getReferenceManager().addMemoryReference(j, d,
                                ghidra.program.model.symbol.RefType.COMPUTED_JUMP, SourceType.USER_DEFINED, 0);
                            if (!dl.contains(d)) dl.add(d);
                        }
                        Function f = getFunctionContaining(j);
                        if (f == null) f = getFunctionBefore(j);
                        new JumpTable(j, dl, true).writeOverride(f);
                        CreateFunctionCmdHelper.fixBody(this, f);
                        break;
                    }
                    case "data": {
                        String[] r = (t.length > 2 ? t[2] : "").split("\\s+");
                        Address d = ds(t[1]);
                        createLabel(d, r[0], true, SourceType.USER_DEFINED);
                        if (r.length > 1) {
                            DataType ty = dt(r[1]);
                            if (r.length > 2) ty = new ArrayDataType(ty, Integer.parseInt(r[2]), ty.getLength());
                            clearListing(d, d.add(ty.getLength() - 1));
                            createData(d, ty);
                        }
                        break;
                    }
                }
            } catch (Exception e) {
                println("ERR on '" + line + "': " + e);
            }
        }

        long lo = Long.parseLong(a[2], 16), hi = Long.parseLong(a[3], 16);
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(new FileWriter(a[1]));
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long off = f.getEntryPoint().getOffset() - 0x10000;
            if (off < lo || off >= hi) continue;
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            out.printf("// ===== %s @ %04x =====\n", f.getName(), off);
            out.println(r != null && r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + (r == null ? "" : r.getErrorMessage()));
        }
        out.close();
    }
}

class CreateFunctionCmdHelper {
    static void fixBody(GhidraScript s, Function f) throws Exception {
        ghidra.app.cmd.function.CreateFunctionCmd.fixupFunctionBody(s.getCurrentProgram(), f, s.getMonitor());
    }
}
