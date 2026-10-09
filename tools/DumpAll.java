// Ghidra headless script: dump function list + decompiled C for every function.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "ghidra_out";
        new File(out).mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter list = new PrintWriter(new FileWriter(out + "/functions.txt"));
        PrintWriter all = new PrintWriter(new FileWriter(out + "/decompiled.c"));
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            list.printf("%s %s %d\n", f.getEntryPoint(), f.getName(), f.getBody().getNumAddresses());
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            all.printf("// ===== %s @ %s =====\n", f.getName(), f.getEntryPoint());
            if (r != null && r.decompileCompleted())
                all.println(r.getDecompiledFunction().getC());
            else
                all.println("// decompile failed\n");
        }
        list.close(); all.close();
    }
}
