// Raw PKLITE output has no executable header, so restore its 0000:0000 entry.
// @category Lemmings
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class SeedEntry extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address entry = toAddr(0);
        disassemble(entry);
        if (getFunctionAt(entry) == null) createFunction(entry, "entry");
        currentProgram.getSymbolTable().addExternalEntryPoint(entry);
    }
}
