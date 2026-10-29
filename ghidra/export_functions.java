// Export every function as TSV: address<TAB>name<TAB>source (text only, no firmware bytes).
// Usage: analyzeHeadless <dir> <proj> -process firmware.elf -noanalysis \
//          -scriptPath <ghidra dir> -postScript export_functions.java <out.tsv>
// @category BTClock

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class export_functions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: export_functions.java <out.tsv>");
            return;
        }
        List<Function> funcs = new ArrayList<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            funcs.add(it.next());
        }
        funcs.sort((a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));
        try (PrintWriter w = new PrintWriter(args[0], "UTF-8")) {
            w.print("# address\tname\tsource (btclock Ghidra function export, addresses and names only)\n");
            for (Function f : funcs) {
                Address a = f.getEntryPoint();
                w.print(a.toString() + "\t" + f.getName() + "\t"
                        + f.getSymbol().getSource().toString() + "\n");
            }
        }
        println("exported " + funcs.size() + " functions to " + args[0]);
    }
}
