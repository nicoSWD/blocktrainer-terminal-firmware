// Re-apply functions.tsv: create missing functions and restore non-default names.
// Idempotent. Usage: analyzeHeadless <dir> <proj> -process firmware.elf -noanalysis \
//          -scriptPath <ghidra dir> -postScript apply_functions.java functions.tsv
// @category BTClock

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class apply_functions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: apply_functions.java <functions.tsv>");
            return;
        }
        int created = 0, renamed = 0, skipped = 0;
        List<String> failed = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new FileReader(args[0]))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] p = line.split("\t");
                if (p.length < 3) { failed.add("badline:" + line); continue; }
                String addrStr = p[0], name = p[1], src = p[2];
                boolean isDefault = src.equals("DEFAULT");
                Address addr = currentProgram.getAddressFactory().getAddress(addrStr);
                if (addr == null) { failed.add(addrStr); continue; }
                Function f = getFunctionAt(addr);
                boolean didSomething = false;
                if (f == null) {
                    if (getInstructionAt(addr) == null) {
                        // A bogus instruction/data item from analysis may overlap the
                        // entry point (e.g. padding decoded as code); clear it first.
                        CodeUnit cu = getInstructionContaining(addr);
                        if (cu == null) cu = getDataContaining(addr);
                        if (cu != null) {
                            clearListing(cu.getMinAddress(), cu.getMaxAddress());
                        }
                        disassemble(addr);
                    }
                    f = createFunction(addr, isDefault ? null : name);
                    if (f == null) { failed.add(addrStr); continue; }
                    created++;
                    didSomething = true;
                }
                if (!isDefault && !f.getName().equals(name)) {
                    try {
                        f.setName(name, SourceType.USER_DEFINED);
                        renamed++;
                        didSomething = true;
                    } catch (Exception e) {
                        failed.add(addrStr + "(" + name + ": " + e.getMessage() + ")");
                        continue;
                    }
                }
                if (!didSomething) skipped++;
            }
        }
        println("apply_functions: created " + created + ", renamed " + renamed
                + ", skipped " + skipped + ", failed " + failed.size());
        for (String s : failed) println("  failed: " + s);
    }
}
