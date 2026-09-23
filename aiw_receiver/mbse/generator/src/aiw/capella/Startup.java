package aiw.capella;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Collections;

import org.eclipse.core.runtime.NullProgressMonitor;
import org.eclipse.sirius.business.api.session.Session;
import org.eclipse.sirius.ui.business.api.viewpoint.ViewpointSelection;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.IStartup;
import org.eclipse.ui.PlatformUI;
import org.polarsys.capella.core.platform.sirius.ui.project.operations.ProjectSessionCreationHelper;

/** Headless driver: -Daiw.mode=create|diagrams, -Daiw.log=<file>. */
public class Startup implements IStartup {
    static PrintWriter log;

    static void log(String s) {
        log.println(s);
        log.flush();
    }

    @Override
    public void earlyStartup() {
        final String mode = System.getProperty("aiw.mode");
        if (mode == null) return;
        try {
            log = new PrintWriter(Files.newBufferedWriter(Path.of(System.getProperty("aiw.log", "/tmp/aiw-capella.log"))));
        } catch (Exception e) {
            return;
        }
        Display.getDefault().asyncExec(() -> {
            try {
                if (mode.equals("create")) create();
                else if (mode.equals("build")) Diagrams.build();
                log("DONE");
            } catch (Throwable t) {
                StringWriter sw = new StringWriter();
                t.printStackTrace(new PrintWriter(sw));
                log("FAILED " + sw);
            } finally {
                log.close();
                PlatformUI.getWorkbench().close();
            }
        });
    }

    static void create() throws Exception {
        String name = System.getProperty("aiw.project", "AIW-Rx");
        ProjectSessionCreationHelper helper = new ProjectSessionCreationHelper(true, true);
        Session s = helper.createFullProject(name, null, Collections.emptyList(),
                ViewpointSelection.getViewpoints("capella"), new NullProgressMonitor());
        s.save(new NullProgressMonitor());
        log("created " + s.getSessionResource().getURI());
        s.close(new NullProgressMonitor());
    }
}
