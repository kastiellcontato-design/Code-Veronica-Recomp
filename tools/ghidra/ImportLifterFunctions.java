/* ImportLifterFunctions — ps3recomp Ghidra headless post-script.
 *
 * Creates a function at every entry the PPU lifter knows about, so Ghidra's
 * view matches src/recomp/ (PS3 calls go through .opd descriptors, which
 * auto-analysis does not follow, so most functions are otherwise missed).
 *
 * Arg 1: text file with one hex address per line (e.g. "00A82718").
 * Functions are named func_XXXXXXXX unless Ghidra already has a real name.
 *
 * Invoke via analyzeHeadless:
 *   analyzeHeadless <proj> <name> -import EBOOT.elf \
 *       -processor PowerPC:BE:64:64-32addr \
 *       -postScript ImportLifterFunctions.java func_addrs.txt
 *
 * @category PS3
 */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class ImportLifterFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: ImportLifterFunctions <addr_list.txt>");
            return;
        }
        List<String> lines = Files.readAllLines(Paths.get(args[0]));
        int created = 0, existing = 0, failed = 0;
        monitor.initialize(lines.size());
        for (String line : lines) {
            monitor.checkCancelled();
            monitor.incrementProgress(1);
            String hex = line.trim();
            if (hex.isEmpty()) continue;
            Address a = toAddr(Long.parseLong(hex, 16));
            if (getInstructionAt(a) == null) disassemble(a);
            Function f = getFunctionAt(a);
            if (f != null) {
                existing++;
                if (f.getName().startsWith("FUN_"))
                    f.setName("func_" + hex.toUpperCase(), SourceType.IMPORTED);
                continue;
            }
            f = createFunction(a, "func_" + hex.toUpperCase());
            if (f != null) created++; else failed++;
        }
        println(String.format("ImportLifterFunctions: created=%d existing=%d failed=%d",
                created, existing, failed));

    }
}
