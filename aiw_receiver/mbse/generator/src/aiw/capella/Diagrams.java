package aiw.capella;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.NullProgressMonitor;
import org.eclipse.emf.ecore.EObject;
import org.eclipse.emf.ecore.resource.Resource;
import org.eclipse.sirius.diagram.description.ContainerMapping;
import org.eclipse.sirius.diagram.description.NodeMapping;
import org.polarsys.capella.core.sirius.analysis.DiagramServices;
import org.polarsys.capella.core.sirius.analysis.ABServices;
import org.eclipse.sirius.diagram.DNodeContainer;
import org.eclipse.sirius.diagram.DragAndDropTarget;
import org.polarsys.capella.core.data.cs.Component;
import org.polarsys.capella.core.data.cs.Part;
import org.polarsys.capella.core.data.epbs.ConfigurationItem;
import org.eclipse.emf.transaction.RecordingCommand;
import org.eclipse.emf.transaction.TransactionalEditingDomain;
import org.eclipse.gef.commands.Command;
import org.eclipse.gmf.runtime.diagram.ui.actions.ActionIds;
import org.eclipse.gmf.runtime.diagram.ui.editparts.DiagramEditPart;
import org.eclipse.gmf.runtime.diagram.ui.parts.DiagramEditor;
import org.eclipse.gmf.runtime.diagram.ui.requests.ArrangeRequest;
import org.eclipse.sirius.business.api.dialect.DialectManager;
import org.eclipse.sirius.business.api.session.Session;
import org.eclipse.sirius.common.tools.api.resource.ImageFileFormat;
import org.eclipse.sirius.diagram.DDiagram;
import org.eclipse.sirius.ui.business.api.dialect.DialectUIManager;
import org.eclipse.sirius.ui.business.api.dialect.ExportFormat;
import org.eclipse.sirius.ui.business.api.dialect.ExportFormat.ExportDocumentFormat;
import org.eclipse.sirius.ui.business.api.viewpoint.ViewpointSelection;
import org.eclipse.sirius.viewpoint.DRepresentation;
import org.eclipse.sirius.viewpoint.description.RepresentationDescription;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.PlatformUI;
import org.polarsys.capella.core.data.capellamodeller.Project;
import org.polarsys.capella.core.platform.sirius.ui.project.operations.ProjectSessionCreationHelper;
import org.polarsys.capella.core.sirius.analysis.CsServices;
import org.polarsys.capella.core.sirius.analysis.DDiagramContents;
import org.polarsys.capella.core.sirius.analysis.FaServices;
import org.polarsys.capella.core.sirius.analysis.InformationServices;

/** Creates the project, builds the model (ModelBuilder) and creates/lays out/exports its diagrams. */
public class Diagrams {
    static final IProgressMonitor PM = new NullProgressMonitor();

    static void flush() {
        Display d = Display.getCurrent();
        for (int i = 0; i < 500 && d.readAndDispatch(); i++) {
        }
    }

    static void build() throws Exception {
        String name = System.getProperty("aiw.project", "AIW-Rx");
        String export = System.getProperty("aiw.export");
        Session session = new ProjectSessionCreationHelper(true, true).createFullProject(name, null,
                Collections.emptyList(), ViewpointSelection.getViewpoints("capella"), PM);
        TransactionalEditingDomain ted = session.getTransactionalEditingDomain();
        Project project = null;
        for (Resource r : session.getSemanticResources())
            for (var o : r.getContents())
                if (o instanceof Project p) project = p;
        final Project prj = project;
        final ModelBuilder mb = new ModelBuilder();
        ted.getCommandStack().execute(new RecordingCommand(ted, "Build AIW-Rx model") {
            @Override
            protected void doExecute() {
                mb.build(prj);
            }
        });
        session.save(PM);
        Startup.log("model built; diagrams to create: " + mb.diagrams.size());

        for (ModelBuilder.DiagramSpec spec : mb.diagrams) {
            try {
                makeDiagram(session, ted, spec, export);
            } catch (Throwable t) {
                Startup.log("DIAGRAM FAILED " + spec.title() + ": " + t);
            }
            PlatformUI.getWorkbench().getActiveWorkbenchWindow().getActivePage().closeAllEditors(false);
            flush();
        }
        session.save(PM);
        session.close(PM);
    }

    static String mappingFor(String kind, String eclass) {
        return switch (kind + "/" + eclass) {
            case "Operational Capabilities Blank/OperationalCapability" -> "COC_OperationalCapabilities";
            case "Operational Capabilities Blank/Entity" -> "COC_OperationalEntities";
            case "Operational Entity Blank/Entity" -> "OAB_Entity1";
            case "Operational Activity Interaction Blank/OperationalActivity" -> "OAIB Operational Activity";
            case "Missions Capabilities Blank/Capability" -> "CapabilityNode4";
            case "Missions Capabilities Blank/Mission" -> "MissionNode4";
            case "Missions Capabilities Blank/SystemComponent" -> "ActorNode4";
            default -> null;
        };
    }

    static void showFunctions(DDiagram d, ModelBuilder.DiagramSpec spec) {
        if (spec.functions.isEmpty()) return;
        DDiagramContents c = FaServices.getFaServices().getDDiagramContents(d);
        ABServices.getService().showABAbstractFunction(spec.functions, c);
        c.commitDeferredActions();
    }

    static void nest(DiagramServices ds, ContainerMapping cm, DDiagram d, DragAndDropTarget parentView, Component ci) {
        for (Part p : ci.getRepresentingParts()) {
            DNodeContainer v = ds.createContainer(cm, p, parentView, d);
            for (Component child : ((ConfigurationItem) ci).getOwnedConfigurationItems()) nest(ds, cm, d, v, child);
        }
    }

    static void makeDiagram(Session session, TransactionalEditingDomain ted, ModelBuilder.DiagramSpec spec,
            String export) throws Exception {
        RepresentationDescription desc = null;
        Collection<RepresentationDescription> avail = DialectManager.INSTANCE
                .getAvailableRepresentationDescriptions(session.getSelectedViewpoints(false), spec.target());
        for (RepresentationDescription d : avail)
            if (spec.kind().equals(d.getName()) || spec.kind().equals(d.getLabel())) desc = d;
        if (desc == null) {
            List<String> names = new ArrayList<>();
            for (RepresentationDescription d : avail) names.add(d.getName());
            Startup.log("NO DESCRIPTION '" + spec.kind() + "' for " + spec.title() + "; available: " + names);
            return;
        }
        final RepresentationDescription dsc = desc;
        final DRepresentation[] made = new DRepresentation[1];
        ted.getCommandStack().execute(new RecordingCommand(ted, "create " + spec.title()) {
            @Override
            protected void doExecute() {
                DRepresentation rep = DialectManager.INSTANCE.createRepresentation(spec.title(), spec.target(), dsc,
                        session, PM);
                made[0] = rep;
                if (!(rep instanceof DDiagram d)) return;
                DiagramServices ds = DiagramServices.getDiagramServices();
                FaServices fa = FaServices.getFaServices();
                switch (spec.mode()) {
                    case "ab" -> {
                        DDiagramContents c = fa.getDDiagramContents(d);
                        CsServices.getService().showABContextualElements(c, spec.elements());
                        c.commitDeferredActions();
                        if (spec.deployHost != null) {
                            DDiagramContents c2 = fa.getDDiagramContents(d);
                            DNodeContainer host = c2.getNodeContainers(spec.deployHost).iterator().next();
                            ContainerMapping cm = (ContainerMapping) ds.getMappingByName(dsc, "PAB_Deployment");
                            for (EObject p : spec.deployed) ds.createContainer(cm, p, host, d);
                        }
                        if (!spec.pceAfter.isEmpty()) {
                            DDiagramContents c3 = fa.getDDiagramContents(d);
                            ABServices.getService().showABComponentExchange(spec.pceAfter, c3);
                            c3.commitDeferredActions();
                        }
                        showFunctions(d, spec);
                    }
                    case "deploy" -> {
                        ContainerMapping pc = (ContainerMapping) ds.getMappingByName(dsc, "PAB_PC");
                        for (EObject p : spec.elements()) ds.createContainer(pc, p, d, d);
                        DDiagramContents c2 = fa.getDDiagramContents(d);
                        DNodeContainer host = c2.getNodeContainers(spec.deployHost).iterator().next();
                        ContainerMapping dm = (ContainerMapping) ds.getMappingByName(dsc, "PAB_Deployment");
                        for (EObject p : spec.deployed) ds.createContainer(dm, p, host, d);
                        DDiagramContents c3 = fa.getDDiagramContents(d);
                        ABServices.getService().showABPhysicalLink(spec.links, c3);
                        c3.commitDeferredActions();
                    }
                    case "df" -> {
                        DDiagramContents c = fa.getDDiagramContents(d);
                        fa.showDFContextualElements(c, spec.elements());
                        c.commitDeferredActions();
                    }
                    case "cdb" -> InformationServices.getService().showCDBContextualElements(d, spec.elements());
                    case "nodes" -> {
                        for (EObject e : spec.elements()) {
                            String m = mappingFor(spec.kind(), e.eClass().getName());
                            var mapping = m == null ? null : ds.getMappingByName(dsc, m);
                            if (mapping instanceof ContainerMapping cm) ds.createContainer(cm, e, d, d);
                            else if (mapping instanceof NodeMapping nm) ds.createNode(nm, e, d, d);
                            else Startup.log("  no mapping for " + e.eClass().getName() + " in " + spec.kind());
                        }
                        showFunctions(d, spec);
                        d.setSynchronized(true);
                    }
                    case "nested" -> {
                        ContainerMapping cm = (ContainerMapping) ds.getMappingByName(dsc, "CI container mapping");
                        nest(ds, cm, d, d, (Component) spec.nestedRoot);
                    }
                    default -> {
                    }
                }
                DialectManager.INSTANCE.refresh(rep, PM);
                if (spec.mode().equals("nodes")) d.setSynchronized(false);
            }
        });
        DRepresentation rep = made[0];
        if (rep == null) {
            Startup.log("CREATE FAILED " + spec.title());
            return;
        }
        IEditorPart ed = DialectUIManager.INSTANCE.openEditor(session, rep, PM);
        flush();
        if (ed instanceof DiagramEditor de) {
            DiagramEditPart dep = de.getDiagramEditPart();
            ArrangeRequest req = new ArrangeRequest(ActionIds.ACTION_ARRANGE_ALL);
            req.setPartsToArrange(new ArrayList<>(dep.getChildren()));
            Command c = dep.getCommand(req);
            if (c != null && c.canExecute()) de.getDiagramEditDomain().getDiagramCommandStack().execute(c);
            flush();
            ed.doSave(PM);
        }
        session.save(PM);
        int n = rep instanceof DDiagram dd ? dd.getDiagramElements().size() : -1;
        Startup.log("created '" + spec.title() + "' [" + spec.kind() + "/" + spec.mode() + "] elements shown: " + n);
        if (export != null) {
            Path out = Path.of(export, spec.title().replaceAll("[^A-Za-z0-9._-]+", "_").replaceAll("^_|_$", "") + ".svg");
            Files.createDirectories(out.getParent());
            DialectUIManager.INSTANCE.export(rep, session, new org.eclipse.core.runtime.Path(out.toString()),
                    new ExportFormat(ExportDocumentFormat.NONE, ImageFileFormat.SVG), PM);
        }
    }
}
