package aiw.capella;

import static aiw.capella.B.*;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.emf.ecore.EObject;
import org.polarsys.capella.core.data.capellamodeller.Project;
import org.polarsys.capella.core.data.capellamodeller.SystemEngineering;
import org.polarsys.capella.core.data.cs.BlockArchitecture;
import org.polarsys.capella.core.data.cs.Component;
import org.polarsys.capella.core.data.cs.Part;
import org.polarsys.capella.core.data.ctx.Capability;
import org.polarsys.capella.core.data.ctx.CapabilityExploitation;
import org.polarsys.capella.core.data.ctx.CapabilityInvolvement;
import org.polarsys.capella.core.data.ctx.CapabilityPkg;
import org.polarsys.capella.core.data.ctx.CtxFactory;
import org.polarsys.capella.core.data.ctx.Mission;
import org.polarsys.capella.core.data.ctx.MissionInvolvement;
import org.polarsys.capella.core.data.ctx.MissionPkg;
import org.polarsys.capella.core.data.ctx.SystemAnalysis;
import org.polarsys.capella.core.data.ctx.SystemComponent;
import org.polarsys.capella.core.data.ctx.SystemComponentPkg;
import org.polarsys.capella.core.data.ctx.SystemFunction;
import org.polarsys.capella.core.data.ctx.SystemFunctionPkg;
import org.polarsys.capella.core.data.epbs.ConfigurationItem;
import org.polarsys.capella.core.data.epbs.ConfigurationItemKind;
import org.polarsys.capella.core.data.epbs.ConfigurationItemPkg;
import org.polarsys.capella.core.data.epbs.EPBSArchitecture;
import org.polarsys.capella.core.data.epbs.EpbsFactory;
import org.polarsys.capella.core.data.epbs.PhysicalArtifactRealization;
import org.polarsys.capella.core.data.fa.AbstractFunction;
import org.polarsys.capella.core.data.fa.ComponentExchange;
import org.polarsys.capella.core.data.fa.FunctionalChain;
import org.polarsys.capella.core.data.fa.FunctionalExchange;
import org.polarsys.capella.core.data.information.DataPkg;
import org.polarsys.capella.core.data.information.ExchangeItem;
import org.polarsys.capella.core.data.information.ExchangeItemElement;
import org.polarsys.capella.core.data.information.ExchangeMechanism;
import org.polarsys.capella.core.data.information.InformationFactory;
import org.polarsys.capella.core.data.information.Property;
import org.polarsys.capella.core.data.information.datatype.DataType;
import org.polarsys.capella.core.data.interaction.AbstractCapabilityRealization;
import org.polarsys.capella.core.data.interaction.AbstractFunctionAbstractCapabilityInvolvement;
import org.polarsys.capella.core.data.interaction.FunctionalChainAbstractCapabilityInvolvement;
import org.polarsys.capella.core.data.interaction.InteractionFactory;
import org.polarsys.capella.core.data.la.CapabilityRealization;
import org.polarsys.capella.core.data.la.CapabilityRealizationPkg;
import org.polarsys.capella.core.data.la.LaFactory;
import org.polarsys.capella.core.data.la.LogicalArchitecture;
import org.polarsys.capella.core.data.la.LogicalComponent;
import org.polarsys.capella.core.data.la.LogicalComponentPkg;
import org.polarsys.capella.core.data.la.LogicalFunction;
import org.polarsys.capella.core.data.la.LogicalFunctionPkg;
import org.polarsys.capella.core.data.oa.CommunicationMean;
import org.polarsys.capella.core.data.oa.Entity;
import org.polarsys.capella.core.data.oa.EntityOperationalCapabilityInvolvement;
import org.polarsys.capella.core.data.oa.EntityPkg;
import org.polarsys.capella.core.data.oa.OaFactory;
import org.polarsys.capella.core.data.oa.OperationalActivity;
import org.polarsys.capella.core.data.oa.OperationalActivityPkg;
import org.polarsys.capella.core.data.oa.OperationalAnalysis;
import org.polarsys.capella.core.data.oa.OperationalCapability;
import org.polarsys.capella.core.data.oa.OperationalCapabilityPkg;
import org.polarsys.capella.core.data.pa.PaFactory;
import org.polarsys.capella.core.data.pa.PhysicalArchitecture;
import org.polarsys.capella.core.data.pa.PhysicalComponent;
import org.polarsys.capella.core.data.pa.PhysicalComponentNature;
import org.polarsys.capella.core.data.pa.PhysicalComponentPkg;
import org.polarsys.capella.core.data.pa.PhysicalFunction;
import org.polarsys.capella.core.data.pa.PhysicalFunctionPkg;
import org.polarsys.capella.core.data.pa.deployment.DeploymentFactory;
import org.polarsys.capella.core.data.pa.deployment.PartDeploymentLink;

/**
 * Builds the AIW-Rx ARCADIA model (OA, SA, LA, PA, EPBS) and returns the diagram specification.
 * Content mirrors aiw_receiver/docs/ARCHITECTURE.md; "Spec reference" property values point at
 * sections of the AIW-Rx engineering specification.
 */
public class ModelBuilder {
    /**
     * One diagram to create: Capella description name, target, title, population mode and elements, plus
     * optional allocated functions to display, parts to deploy into a host part, and nested CI parts.
     */
    public static class DiagramSpec {
        final String kind, title, mode;
        final EObject target;
        final List<EObject> elements;
        List<EObject> functions = List.of();
        EObject deployHost;
        List<EObject> deployed = List.of();
        EObject nestedRoot;
        List<EObject> pceAfter = List.of();
        List<EObject> links = List.of();

        DiagramSpec(String kind, EObject target, String title, String mode, List<EObject> elements) {
            this.kind = kind;
            this.target = target;
            this.title = title;
            this.mode = mode;
            this.elements = elements;
        }

        DiagramSpec functions(java.util.Collection<? extends EObject> f) {
            functions = new ArrayList<>(f);
            return this;
        }

        String kind() { return kind; }
        String title() { return title; }
        String mode() { return mode; }
        EObject target() { return target; }
        List<EObject> elements() { return elements; }
    }

    final List<DiagramSpec> diagrams = new ArrayList<>();

    final OaFactory OA = OaFactory.eINSTANCE;
    final CtxFactory CTX = CtxFactory.eINSTANCE;
    final LaFactory LA = LaFactory.eINSTANCE;
    final PaFactory PA = PaFactory.eINSTANCE;

    OperationalAnalysis oa;
    SystemAnalysis sa;
    LogicalArchitecture la;
    PhysicalArchitecture pa;
    EPBSArchitecture epbs;

    // --- helpers to collect the parts of components for AB diagrams
    static List<EObject> partsOf(Component... cs) {
        List<EObject> l = new ArrayList<>();
        for (Component c : cs) l.addAll(c.getRepresentingParts());
        return l;
    }

    void build(Project project) {
        SystemEngineering se = (SystemEngineering) project.getOwnedModelRoots().get(0);
        for (org.polarsys.capella.core.data.capellacore.ModellingArchitecture a : se.getOwnedArchitectures()) {
            if (a instanceof OperationalAnalysis x) oa = x;
            else if (a instanceof SystemAnalysis x) sa = x;
            else if (a instanceof LogicalArchitecture x) la = x;
            else if (a instanceof PhysicalArchitecture x) pa = x;
            else if (a instanceof EPBSArchitecture x) epbs = x;
        }
        se.setDescription("<p>ARCADIA model of AIW-Rx, the standalone C++20 software-defined radio receiver that "
                + "replaces the GNU Radio flowgraph hwil_conventional_evaluation_rx.grc. Source: aiw_receiver/ in "
                + "github.com/peyam/qutip-lectures. See docs/ARCHITECTURE.md.</p>");
        buildOA();
        buildSA();
        buildLA();
        buildPA();
        buildEPBS();
    }

    // =====================================================================================
    // Operational Analysis
    // =====================================================================================
    Entity eEngineer, eReviewer, eTransmitter, eChannel, eStation;
    OperationalCapability ocEvaluate, ocMonitor, ocReplay;
    Map<String, OperationalActivity> act = new LinkedHashMap<>();
    List<FunctionalExchange> oaFE = new ArrayList<>();
    List<ComponentExchange> oaCM = new ArrayList<>();

    void buildOA() {
        oa.setDescription("<p>The problem space: an RF test laboratory evaluates the quality of the AIW "
                + "256-QAM link in hardware-in-the-loop tests.</p>");
        EntityPkg ep = oa.getOwnedEntityPkg();
        eEngineer = entity(ep, "Test Engineer", true, "Plans and runs link tests, watches results live and adjusts the radio.");
        eReviewer = entity(ep, "Test Reviewer", true, "Programme stakeholder who reviews test evidence and accepts results.");
        eTransmitter = entity(ep, "AIW Transmitter Station", false, "Generates and radiates the framed AIW 256-QAM test waveform.");
        eChannel = entity(ep, "Radio Propagation Channel", false, "Cable, attenuator or over-the-air path: adds loss, noise, multipath and frequency error.");
        eStation = entity(ep, "Link Evaluation Station", false, "Receives the waveform and measures link quality. Realised by AIW-Rx.");
        eEngineer.setHuman(true);
        eReviewer.setHuman(true);

        OperationalActivity root = (OperationalActivity) ((OperationalActivityPkg) oa.getOwnedFunctionPkg())
                .getOwnedOperationalActivities().get(0);
        OperationalActivity plan = oact(root, "Plan and configure test", "Choose frequency, gain, duration and pass criteria.");
        OperationalActivity observe = oact(root, "Observe live link quality", "Watch BER, SNR, MER and the constellation during the run.");
        OperationalActivity adjust = oact(root, "Adjust receiver settings", "Retune frequency or gain in response to what is observed.");
        OperationalActivity transmit = oact(root, "Transmit framed test waveform", "Radiate UW-framed, RS-coded 256-QAM frames carrying a known payload.");
        OperationalActivity propagate = oact(root, "Propagate and impair signal", "Attenuate, delay, echo and add noise and frequency offset to the signal.");
        OperationalActivity receive = oact(root, "Receive RF signal", "Capture the RF signal at the evaluation station.");
        OperationalActivity recover = oact(root, "Recover test payload", "Synchronise, equalise, demodulate and error-correct the frames.");
        OperationalActivity measure = oact(root, "Measure link quality", "Compare recovered payloads with the known payload and estimate SNR and MER.");
        OperationalActivity record = oact(root, "Record test evidence", "Keep logs, summaries and captures of the run.");
        OperationalActivity review = oact(root, "Review test evidence", "Assess results against the acceptance criteria.");
        alloc(eEngineer, plan, observe, adjust);
        alloc(eTransmitter, transmit);
        alloc(eChannel, propagate);
        alloc(eStation, receive, recover, measure, record);
        alloc(eReviewer, review);

        FunctionalExchange i1 = fe(root, plan, transmit, "Transmitter settings", null);
        FunctionalExchange i2 = fe(root, plan, receive, "Receiver settings", null);
        FunctionalExchange i3 = fe(root, transmit, propagate, "Radiated waveform", null);
        FunctionalExchange i4 = fe(root, propagate, receive, "Received RF signal", null);
        FunctionalExchange i5 = fe(root, receive, recover, "Captured signal", null);
        FunctionalExchange i6 = fe(root, recover, measure, "Recovered frames", null);
        FunctionalExchange i7 = fe(root, measure, observe, "Live link metrics", null);
        FunctionalExchange i8 = fe(root, observe, adjust, "Tuning decision", null);
        FunctionalExchange i9 = fe(root, adjust, receive, "Tuning commands", null);
        FunctionalExchange i10 = fe(root, measure, record, "Quality results", null);
        FunctionalExchange i11 = fe(root, record, review, "Test report", null);

        CommunicationMean cm1 = cmean(ep, eEngineer, eTransmitter, "Transmitter control", i1);
        CommunicationMean cm2 = cmean(ep, eEngineer, eStation, "Operator console", i2, i9);
        CommunicationMean cm3 = cmean(ep, eTransmitter, eChannel, "RF emission", i3);
        CommunicationMean cm4 = cmean(ep, eChannel, eStation, "RF reception", i4);
        CommunicationMean cm5 = cmean(ep, eStation, eEngineer, "Results display", i7);
        CommunicationMean cm6 = cmean(ep, eStation, eReviewer, "Report delivery", i11);
        oaFE.addAll(List.of(i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11));
        oaCM.addAll(List.of(cm1, cm2, cm3, cm4, cm5, cm6));

        OperationalCapabilityPkg ocp = (OperationalCapabilityPkg) oa.getOwnedAbstractCapabilityPkg();
        ocEvaluate = ocap(ocp, "Evaluate AIW link quality",
                "Quantify the bit error rate of the AIW link before and after FEC under lab conditions.",
                List.of(eEngineer, eTransmitter, eChannel, eStation, eReviewer),
                List.of(plan, transmit, propagate, receive, recover, measure, record, review));
        ocMonitor = ocap(ocp, "Monitor the link in real time",
                "See link health while the test runs and react by retuning.",
                List.of(eEngineer, eStation), List.of(observe, adjust, receive, measure));
        ocReplay = ocap(ocp, "Reproduce a recorded test session",
                "Analyse a previously recorded session offline, repeatably.",
                List.of(eEngineer, eStation, eReviewer), List.of(recover, measure, record, review));

        diagrams.add(new DiagramSpec("Operational Capabilities Blank", ocp, "[OCB] Operational Capabilities", "nodes",
                List.of(eEngineer, eTransmitter, eChannel, eStation, eReviewer, ocEvaluate, ocMonitor, ocReplay)));
        diagrams.add(new DiagramSpec("Operational Entity Blank", ep, "[OEB] Operational Entities", "nodes",
                List.of(eEngineer, eTransmitter, eChannel, eStation, eReviewer))
                .functions(List.of(plan, observe, adjust, transmit, propagate, receive, recover, measure, record, review)));
        diagrams.add(new DiagramSpec("Operational Activity Interaction Blank", root, "[OAIB] Operational Activities", "nodes",
                List.of(plan, observe, adjust, transmit, propagate, receive, recover, measure, record, review)));
        diagrams.add(new DiagramSpec("Contextual Operational Capability", ocEvaluate, "[COC] Evaluate AIW link quality", "refresh", List.of()));
    }

    Entity entity(EntityPkg pkg, String name, boolean actor, String desc) {
        Entity e = n(OA.createEntity(), name, desc);
        e.setActor(actor);
        pkg.getOwnedEntities().add(e);
        partIn(pkg, e);
        return e;
    }

    OperationalActivity oact(OperationalActivity root, String name, String desc) {
        OperationalActivity a = fn(root, OA.createOperationalActivity(), name, desc);
        act.put(name, a);
        return a;
    }

    CommunicationMean cmean(EntityPkg pkg, Entity from, Entity to, String name, FunctionalExchange... fes) {
        CommunicationMean c = n(OA.createCommunicationMean(), name, null);
        c.setSource(from);
        c.setTarget(to);
        pkg.getOwnedCommunicationMeans().add(c);
        allocFE(c, fes);
        return c;
    }

    OperationalCapability ocap(OperationalCapabilityPkg pkg, String name, String desc, List<Entity> ents,
            List<OperationalActivity> acts) {
        OperationalCapability c = n(OA.createOperationalCapability(), name, desc);
        pkg.getOwnedOperationalCapabilities().add(c);
        for (Entity e : ents) {
            EntityOperationalCapabilityInvolvement i = OA.createEntityOperationalCapabilityInvolvement();
            i.setInvolved(e);
            c.getOwnedEntityOperationalCapabilityInvolvements().add(i);
        }
        involveFns(c.getOwnedAbstractFunctionAbstractCapabilityInvolvements(), new ArrayList<>(acts));
        return c;
    }

    static void involveFns(List<AbstractFunctionAbstractCapabilityInvolvement> owner, List<AbstractFunction> fns) {
        for (AbstractFunction f : fns) {
            AbstractFunctionAbstractCapabilityInvolvement i = InteractionFactory.eINSTANCE
                    .createAbstractFunctionAbstractCapabilityInvolvement();
            i.setInvolved(f);
            owner.add(i);
        }
    }

    // =====================================================================================
    // System Analysis
    // =====================================================================================
    SystemComponent system, aOperator, aTransmitter, aUsrp, aBrowser, aFiles, aAutomation;
    Map<String, SystemFunction> sf = new LinkedHashMap<>();
    Map<String, FunctionalExchange> sfe = new LinkedHashMap<>();
    Map<String, ComponentExchange> sce = new LinkedHashMap<>();
    SystemFunction sRoot;
    List<Capability> caps = new ArrayList<>();

    void buildSA() {
        sa.setDescription("<p>AIW-Rx as a black box: what the receiver software must do for its users and "
                + "neighbouring systems.</p>");
        SystemComponentPkg pkg = sa.getOwnedSystemComponentPkg();
        system = (SystemComponent) sa.getSystem();
        n(system, "AIW-Rx Receiver",
                "Standalone real-time software receiver for the AIW 256-QAM test waveform (aiw_rx).");
        for (Part p : system.getRepresentingParts()) p.setName(system.getName());
        realizeComp(system, eStation);

        aOperator = actor(pkg, "Test Operator", true, "Runs aiw_rx, watches results and adjusts settings.");
        aTransmitter = actor(pkg, "AIW Transmitter", false, "Transmits UW-framed, RS(255,239)-coded 256-QAM at 350 kBd.");
        aUsrp = actor(pkg, "USRP Radio", false, "Ettus USRP (serial 3273A14, RX2): tunes, digitises and streams IQ at 1.4 MSps.");
        aBrowser = actor(pkg, "Web Browser", false, "Displays the live dashboard served by aiw_rx --ui.");
        aFiles = actor(pkg, "File System", false, "Holds capture files (complex float32) and CSV logs.");
        aAutomation = actor(pkg, "Test Automation", false, "CTest / CI harness that runs aiw_rx and checks its exit status.");
        realizeComp(aOperator, eEngineer);
        realizeComp(aTransmitter, eTransmitter);

        sRoot = (SystemFunction) ((SystemFunctionPkg) sa.getOwnedFunctionPkg()).getOwnedSystemFunctions().get(0);
        // System functions
        sfn(system, "Acquire IQ samples", "Read 8192-sample chunks from the selected source and timestamp them.", "3, 7", receive());
        sfn(system, "Condition signal", "Remove DC, translate the +300 kHz carrier to baseband, low-pass filter and normalise gain.", "4.1-4.3", receive());
        sfn(system, "Recover symbol timing", "Matched-filter and resample to one sample per symbol with a 32-branch polyphase clock synchroniser.", "4.4", recover());
        sfn(system, "Detect and extract frames", "Correlate with the unique word, confirm with the trailing UW and extract 502-symbol segments.", "5.1-5.2", recover());
        sfn(system, "Equalise and correct carrier", "Estimate CFO from both UWs, fit a 5-tap least-squares equaliser and remove residual phase.", "5.3", recover());
        sfn(system, "Demodulate symbols", "Slice 256-QAM symbols to bytes.", "6.1", recover());
        sfn(system, "Correct errors", "Decode the shortened RS(255,239) codewords (8-byte correction).", "6.2", recover());
        sfn(system, "Measure bit error rate", "Compare received and decoded bytes with the ground-truth payload (pre- and post-FEC BER).", "6.3", measure());
        sfn(system, "Estimate SNR", "Radiometric in-band SNR from signal-band and noise-band power.", "6.4", measure());
        sfn(system, "Report metrics", "Aggregate per-interval metrics; print console lines, write CSV, set the exit status.", "3", measure());
        sfn(system, "Serve dashboard", "Serve the live web dashboard and its JSON API.", "-", act.get("Observe live link quality"));
        sfn(system, "Apply runtime control", "Validate and apply frequency, gain or simulator channel changes while running.", "-", act.get("Adjust receiver settings"));
        sfn(system, "Replay capture", "Read a recorded complex-float32 capture as an input source.", "-", receive());
        sfn(system, "Simulate transmitter and channel", "Generate the AIW waveform with configurable impairments for testing without hardware.", "9", null);
        // Actor functions
        sfn(aTransmitter, "Emit AIW waveform", "Radiate the framed test waveform.", null, act.get("Transmit framed test waveform"));
        sfn(aUsrp, "Digitise RF signal", "Tune, filter and sample the RF input; stream fc32 IQ.", null, act.get("Receive RF signal"));
        sfn(aUsrp, "Retune radio", "Change centre frequency or RF gain on request.", null, null);
        sfn(aOperator, "Configure run", "Select source, frequency, gain and options on the command line.", null, act.get("Plan and configure test"));
        sfn(aOperator, "Monitor results", "Read the console status lines and summary.", null, act.get("Observe live link quality"));
        sfn(aBrowser, "Render dashboard", "Display tiles, constellation, spectrum and trends; send control requests.", null, act.get("Observe live link quality"));
        sfn(aFiles, "Store files", "Persist captures and CSV logs.", null, act.get("Record test evidence"));
        sfn(aAutomation, "Check exit status", "Launch aiw_rx and evaluate its exit code (e.g. --expect-ber0).", null, act.get("Review test evidence"));

        sx("Emit AIW waveform", "Digitise RF signal", "RF waveform");
        sx("Digitise RF signal", "Acquire IQ samples", "IQ stream (fc32)");
        sx("Store files", "Replay capture", "Capture file");
        sx("Replay capture", "Acquire IQ samples", "Replayed samples");
        sx("Simulate transmitter and channel", "Acquire IQ samples", "Simulated samples");
        sx("Configure run", "Acquire IQ samples", "Run configuration");
        sx("Acquire IQ samples", "Condition signal", "Sample chunks");
        sx("Condition signal", "Recover symbol timing", "Baseband samples");
        sx("Condition signal", "Estimate SNR", "DC-blocked samples");
        sx("Recover symbol timing", "Detect and extract frames", "Symbols");
        sx("Detect and extract frames", "Equalise and correct carrier", "Segments");
        sx("Equalise and correct carrier", "Demodulate symbols", "Payload symbols");
        sx("Demodulate symbols", "Correct errors", "Codewords");
        sx("Correct errors", "Measure bit error rate", "Decoded payload");
        sx("Measure bit error rate", "Report metrics", "Frame statistics");
        sx("Estimate SNR", "Report metrics", "SNR estimate");
        sx("Report metrics", "Monitor results", "Console report");
        sx("Report metrics", "Store files", "CSV log");
        sx("Report metrics", "Check exit status", "Exit status");
        sx("Report metrics", "Serve dashboard", "Metrics snapshot");
        sx("Serve dashboard", "Render dashboard", "Dashboard data");
        sx("Render dashboard", "Apply runtime control", "Control request");
        sx("Apply runtime control", "Retune radio", "Tuning command");
        sx("Apply runtime control", "Simulate transmitter and channel", "Channel settings");

        List<ComponentExchange> own = pkg.getOwnedComponentExchanges();
        sce.put("RF link", ce(own, aTransmitter, aUsrp, "RF link", "Antenna or cabled RF path at 917 MHz.", sfe.get("RF waveform")));
        sce.put("Sample stream", ce(own, aUsrp, system, "Sample stream", "UHD rx_streamer over USB 3 / GbE, fc32 at 1.4 MSps.", sfe.get("IQ stream (fc32)")));
        sce.put("Radio control", ce(own, system, aUsrp, "Radio control", "UHD multi_usrp control calls.", sfe.get("Tuning command")));
        sce.put("Command line", ce(own, aOperator, system, "Command line", "Options and signals (Ctrl+C).", sfe.get("Run configuration")));
        sce.put("Console", ce(own, system, aOperator, "Console", "Status lines and summary on stdout.", sfe.get("Console report")));
        sce.put("HTTP out", ce(own, system, aBrowser, "Dashboard HTTP", "HTML page and JSON API on 127.0.0.1:8080.", sfe.get("Dashboard data")));
        sce.put("HTTP in", ce(own, aBrowser, system, "Control POST", "POST /api/control with X-AIW-Control header.", sfe.get("Control request")));
        sce.put("File read", ce(own, aFiles, system, "Capture input", "Raw interleaved complex float32 file.", sfe.get("Capture file")));
        sce.put("File write", ce(own, system, aFiles, "CSV output", "Per-interval metrics CSV.", sfe.get("CSV log")));
        sce.put("Exit code", ce(own, system, aAutomation, "Process exit code", "0 pass, 1 error or BER fail, 2 bad arguments.", sfe.get("Exit status")));

        Startup.log("SA->OA exchange realizations: FE " + autoRealizeFE(sfe.values(), oaFE) + ", CE "
                + autoRealizeCE(sce.values(), oaCM));

        // Functional chains
        FunctionalChain fcDecode = chain(sRoot, "Live decode and measure",
                "Nominal thread of the receiver: from RF waveform to the operator's console report.",
                sfs("Emit AIW waveform", "Digitise RF signal", "Acquire IQ samples", "Condition signal",
                        "Recover symbol timing", "Detect and extract frames", "Equalise and correct carrier",
                        "Demodulate symbols", "Correct errors", "Measure bit error rate", "Report metrics",
                        "Monitor results"),
                sfes("RF waveform", "IQ stream (fc32)", "Sample chunks", "Baseband samples", "Symbols", "Segments",
                        "Payload symbols", "Codewords", "Decoded payload", "Frame statistics", "Console report"));
        FunctionalChain fcRetune = chain(sRoot, "Retune from dashboard",
                "The operator changes frequency or gain from the browser while receiving.",
                sfs("Report metrics", "Serve dashboard", "Render dashboard", "Apply runtime control", "Retune radio"),
                sfes("Metrics snapshot", "Dashboard data", "Control request", "Tuning command"));
        FunctionalChain fcReplay = chain(sRoot, "Replay and measure",
                "A recorded capture is decoded and measured offline.",
                sfs("Store files", "Replay capture", "Acquire IQ samples", "Condition signal", "Recover symbol timing"),
                sfes("Capture file", "Replayed samples", "Sample chunks", "Baseband samples"));

        // Mission and capabilities
        MissionPkg mp = sa.getOwnedMissionPkg();
        Mission mission = n(CTX.createMission(), "Evaluate AIW link performance in HWIL tests",
                "Provide trustworthy, real-time measurements of the AIW link for hardware-in-the-loop evaluation.");
        mp.getOwnedMissions().add(mission);
        for (Component a : List.of(aOperator, aTransmitter, aUsrp, aBrowser, aFiles, aAutomation)) {
            MissionInvolvement mi = CTX.createMissionInvolvement();
            mi.setInvolved((org.polarsys.capella.core.data.capellacore.InvolvedElement) a);
            mission.getOwnedMissionInvolvements().add(mi);
        }
        CapabilityPkg cp = (CapabilityPkg) sa.getOwnedAbstractCapabilityPkg();
        Capability c1 = cap(cp, mission, "Receive and decode AIW frames", "Continuously acquire, synchronise, equalise, demodulate and error-correct AIW frames in real time (< 20 ms latency).",
                ocEvaluate, List.of(aTransmitter, aUsrp, aOperator), fcDecode,
                "Acquire IQ samples", "Condition signal", "Recover symbol timing", "Detect and extract frames",
                "Equalise and correct carrier", "Demodulate symbols", "Correct errors");
        Capability c2 = cap(cp, mission, "Measure link quality", "Compute pre-/post-FEC BER against the ground truth, radiometric SNR and UW MER.",
                ocEvaluate, List.of(aTransmitter, aUsrp), null, "Measure bit error rate", "Estimate SNR");
        Capability c3 = cap(cp, mission, "Report results", "Console lines and summary, CSV log and a pass/fail exit status.",
                ocEvaluate, List.of(aOperator, aFiles, aAutomation), null, "Report metrics");
        Capability c4 = cap(cp, mission, "Provide live dashboard and control", "Browser dashboard with live plots and runtime frequency/gain/channel control.",
                ocMonitor, List.of(aOperator, aBrowser, aUsrp), fcRetune, "Serve dashboard", "Apply runtime control");
        Capability c5 = cap(cp, mission, "Replay recorded captures", "Decode a recorded capture file instead of live input.",
                ocReplay, List.of(aFiles), fcReplay, "Replay capture");
        Capability c6 = cap(cp, mission, "Self-test with simulated transmitter", "Verify the whole chain without hardware using the built-in transmitter and channel model.",
                null, List.of(aAutomation), null, "Simulate transmitter and channel");
        caps.addAll(List.of(c1, c2, c3, c4, c5, c6));

        List<EObject> mcb = new ArrayList<>(List.of(mission));
        mcb.addAll(caps);
        mcb.addAll(List.of(aOperator, aTransmitter, aUsrp, aBrowser, aFiles, aAutomation));
        diagrams.add(new DiagramSpec("Missions Capabilities Blank", cp, "[MCB] Missions and Capabilities", "nodes", mcb));
        diagrams.add(new DiagramSpec("Contextual Capability", c1, "[CC] Receive and decode AIW frames", "refresh", List.of()));
        List<EObject> sdf = new ArrayList<>(sf.values());
        sdf.addAll(sfe.values());
        diagrams.add(new DiagramSpec("System Data Flow Blank", sRoot, "[SDFB] System Functions", "df", sdf));
        List<EObject> sab = new ArrayList<>(partsOf(system, aOperator, aTransmitter, aUsrp, aBrowser, aFiles, aAutomation));
        sab.addAll(sce.values());
        diagrams.add(new DiagramSpec("System Architecture Blank", system, "[SAB] AIW-Rx System Context", "ab", sab)
                .functions(sf.values()));
        diagrams.add(new DiagramSpec("Functional Chain Description", fcDecode, "[FCD] Live decode and measure", "refresh", List.of()));
    }

    OperationalActivity receive() {
        return act.get("Receive RF signal");
    }

    OperationalActivity recover() {
        return act.get("Recover test payload");
    }

    OperationalActivity measure() {
        return act.get("Measure link quality");
    }

    SystemComponent actor(SystemComponentPkg pkg, String name, boolean human, String desc) {
        SystemComponent a = n(CTX.createSystemComponent(), name, desc);
        a.setActor(true);
        a.setHuman(human);
        pkg.getOwnedSystemComponents().add(a);
        partIn(pkg, a);
        return a;
    }

    SystemFunction sfn(Component owner, String name, String desc, String spec, OperationalActivity realizes) {
        SystemFunction f = fn(sRoot, CTX.createSystemFunction(), name, desc);
        alloc(owner, f);
        if (spec != null) prop(f, "Spec reference", spec.equals("-") ? "added (not in spec)" : "§" + spec);
        if (realizes != null) realizeFn(f, realizes);
        sf.put(name, f);
        return f;
    }

    void sx(String from, String to, String name) {
        sfe.put(name, fe(sRoot, sf.get(from), sf.get(to), name, null));
    }

    List<AbstractFunction> sfs(String... names) {
        List<AbstractFunction> l = new ArrayList<>();
        for (String s : names) l.add(sf.get(s));
        return l;
    }

    List<FunctionalExchange> sfes(String... names) {
        List<FunctionalExchange> l = new ArrayList<>();
        for (String s : names) l.add(sfe.get(s));
        return l;
    }

    Capability cap(CapabilityPkg pkg, Mission m, String name, String desc, OperationalCapability realizes,
            List<SystemComponent> actors, FunctionalChain chain, String... fnNames) {
        Capability c = n(CTX.createCapability(), name, desc);
        pkg.getOwnedCapabilities().add(c);
        CapabilityExploitation x = CTX.createCapabilityExploitation();
        x.setCapability(c);
        m.getOwnedCapabilityExploitations().add(x);
        List<SystemComponent> inv = new ArrayList<>(actors);
        inv.add(0, system);
        for (SystemComponent a : inv) {
            CapabilityInvolvement ci = CTX.createCapabilityInvolvement();
            ci.setInvolved(a);
            c.getOwnedCapabilityInvolvements().add(ci);
        }
        involveFns(c.getOwnedAbstractFunctionAbstractCapabilityInvolvements(), new ArrayList<>(sfs(fnNames)));
        if (chain != null) {
            FunctionalChainAbstractCapabilityInvolvement fi = InteractionFactory.eINSTANCE
                    .createFunctionalChainAbstractCapabilityInvolvement();
            fi.setInvolved(chain);
            c.getOwnedFunctionalChainAbstractCapabilityInvolvements().add(fi);
        }
        if (realizes != null) {
            AbstractCapabilityRealization r = InteractionFactory.eINSTANCE.createAbstractCapabilityRealization();
            r.setSourceElement(c);
            r.setTargetElement(realizes);
            c.getOwnedAbstractCapabilityRealizations().add(r);
        }
        return c;
    }

    // =====================================================================================
    // Logical Architecture
    // =====================================================================================
    LogicalComponent lSystem, lIngest, lFront, lSync, lDecode, lRadio, lDispatch, lDash, lSource;
    Map<String, LogicalComponent> lActors = new LinkedHashMap<>();
    Map<String, LogicalFunction> lf = new LinkedHashMap<>();
    Map<String, FunctionalExchange> lfe = new LinkedHashMap<>();
    Map<String, ComponentExchange> lce = new LinkedHashMap<>();
    Map<LogicalFunction, LogicalComponent> lAlloc = new LinkedHashMap<>();
    LogicalFunction lRoot;

    void buildLA() {
        la.setDescription("<p>How AIW-Rx is decomposed: one logical component per pipeline stage, joined by "
                + "lock-free single-producer/single-consumer queues. See ARCHITECTURE.md sections 3-10.</p>");
        LogicalComponentPkg pkg = la.getOwnedLogicalComponentPkg();
        lSystem = (LogicalComponent) la.getSystem();
        n(lSystem, "AIW-Rx Receiver", "Logical decomposition of the aiw_rx process.");
        for (Part p : lSystem.getRepresentingParts()) p.setName(lSystem.getName());

        for (SystemComponent a : List.of(aOperator, aTransmitter, aUsrp, aBrowser, aFiles, aAutomation)) {
            LogicalComponent l = n(LA.createLogicalComponent(), a.getName(), null);
            l.setDescription(a.getDescription());
            l.setActor(true);
            l.setHuman(a.isHuman());
            pkg.getOwnedLogicalComponents().add(l);
            partIn(pkg, l);
            realizeComp(l, a);
            lActors.put(a.getName(), l);
        }

        lIngest = lc("Sample Ingestion (T1)", "Reads 8192-sample chunks, timestamps them and pushes them into q_in; drops chunks from a live source when q_in is full.");
        lFront = lc("Front End (T2)", "DC blocker, -300 kHz mixer with 93-tap low-pass, AGC and 32-branch PFB clock synchroniser (the RRC matched filter).");
        lSync = lc("Frame Synchroniser and Equaliser (T3)", "Normalised UW correlator with trailing-UW confirmation, segment extraction, two-pass CFO + 5-tap LLS equaliser, demultiplexer.");
        lDecode = lc("Decoder (T4)", "256-QAM slicer, batch of 8 codewords, shortened RS(255,239) decoder and BER against ground truth.");
        lRadio = lc("Radiometer (T5)", "SNR from +60..+540 kHz and -540..-60 kHz band powers; 1024-point spectrum for the dashboard.");
        lDispatch = lc("Metrics Dispatcher", "Main thread: turns atomic counters into per-interval console lines, CSV rows, dashboard history, summary and exit code.");
        lDash = lc("Dashboard Server", "Single-threaded HTTP/1.1 server with the embedded page and JSON API; CSRF and DNS-rebinding guards.");
        lSource = lc("Sample Source Adapter", "SampleSource implementations: UsrpSource (UHD), FileSource and SimSource (TxSimulator).");

        lRoot = (LogicalFunction) ((LogicalFunctionPkg) la.getOwnedFunctionPkg()).getOwnedLogicalFunctions().get(0);
        // Internal logical functions: name, component, realised system function
        lfn(lSource, "Stream from USRP", "Acquire IQ samples", "UHD rx_streamer, fc32, 8192-sample recv; counts overflows.");
        lfn(lSource, "Read capture file", "Replay capture", "Raw interleaved complex float32; optional loop.");
        lfn(lSource, "Generate simulated signal", "Simulate transmitter and channel", "RRC-shaped frames with CFO, ppm, echo, DC and AWGN.");
        lfn(lSource, "Apply source control", "Apply runtime control", "USRP frequency/gain or simulator Es/N0/CFO requests.");
        lfn(lIngest, "Read and timestamp chunk", "Acquire IQ samples", "t_ingest carried to every later stage for latency measurement.");
        lfn(lFront, "Block DC", "Condition signal", "y[n] = x[n] - x[n-1] + 0.96875 y[n-1].");
        lfn(lFront, "Translate and channel-filter", "Condition signal", "Mix by -300 kHz, 93-tap Hamming low-pass (B/2 = 252 kHz).");
        lfn(lFront, "Control gain", "Condition signal", "Multiplicative AGC, attack/decay 1e-3.");
        lfn(lFront, "Recover timing (PFB)", "Recover symbol timing", "32 x 65-tap RRC bank + derivative bank; 2nd-order loop; 4 -> 1 sps.");
        lfn(lSync, "Correlate unique word", "Detect and extract frames", "m[n] = |C|^2/(L E) > 0.35 with a trailing-UW check at +359.");
        lfn(lSync, "Extract segment", "Detect and extract frames", "502 symbols + 4-symbol margins.");
        lfn(lSync, "Estimate and remove CFO", "Equalise and correct carrier", "Coarse lag-32 and fine lead/trail estimators.");
        lfn(lSync, "Equalise channel (LLS)", "Equalise and correct carrier", "w = (Y^H Y + 1e-4 I)^-1 Y^H d, decision delay 2.");
        lfn(lDecode, "Slice 256-QAM", "Demodulate symbols", "Nearest odd level, natural-binary byte mapping.");
        lfn(lDecode, "Decode Reed-Solomon", "Correct errors", "BM + Chien + Forney, 39-byte shortening, fcr 0.");
        lfn(lDecode, "Compare with ground truth", "Measure bit error rate", "Pre-FEC over 1728 bits, post-FEC over 1600 bits.");
        lfn(lRadio, "Estimate SNR", "Estimate SNR", "EMA over 1e5 samples, reported per 1e4 samples.");
        lfn(lRadio, "Estimate spectrum", "Serve dashboard", "1024-point Hann FFT, dBFS, for the dashboard.");
        lfn(lDispatch, "Aggregate metrics", "Report metrics", "Per-interval deltas of atomic counters.");
        lfn(lDispatch, "Write console, CSV and exit code", "Report metrics", "Console line, CSV row, final summary, exit status.");
        lfn(lDash, "Serve dashboard API", "Serve dashboard", "GET /, /api/status, /api/constellation, /api/spectrum, /api/history.");
        lfn(lDash, "Validate control request", "Apply runtime control", "X-AIW-Control header, Host check, range checks.");
        // Actor functions (mirrors of the system-level actor functions)
        Map<String, String> actorFns = new LinkedHashMap<>();
        actorFns.put("Emit AIW waveform", "AIW Transmitter");
        actorFns.put("Digitise RF signal", "USRP Radio");
        actorFns.put("Retune radio", "USRP Radio");
        actorFns.put("Configure run", "Test Operator");
        actorFns.put("Monitor results", "Test Operator");
        actorFns.put("Render dashboard", "Web Browser");
        actorFns.put("Store files", "File System");
        actorFns.put("Check exit status", "Test Automation");
        for (var e : actorFns.entrySet()) {
            SystemFunction up = sf.get(e.getKey());
            lfn(lActors.get(e.getValue()), e.getKey(), e.getKey(), up.getDescription().replaceAll("</?p>", ""));
        }

        // Logical functional exchanges: from, to, name, realised system exchange
        lx("Emit AIW waveform", "Digitise RF signal", "RF waveform", "RF waveform");
        lx("Digitise RF signal", "Stream from USRP", "IQ stream (fc32)", "IQ stream (fc32)");
        lx("Store files", "Read capture file", "Capture file", "Capture file");
        lx("Stream from USRP", "Read and timestamp chunk", "USRP samples", "Sample chunks");
        lx("Read capture file", "Read and timestamp chunk", "Replayed samples", "Replayed samples");
        lx("Generate simulated signal", "Read and timestamp chunk", "Simulated samples", "Simulated samples");
        lx("Configure run", "Read and timestamp chunk", "Run configuration", "Run configuration");
        lx("Read and timestamp chunk", "Block DC", "SampleChunk (q_in)", "Sample chunks");
        lx("Block DC", "Translate and channel-filter", "DC-free samples", "Baseband samples");
        lx("Block DC", "Estimate SNR", "SampleChunk (q_snr)", "DC-blocked samples");
        lx("Block DC", "Estimate spectrum", "Spectrum input", "DC-blocked samples");
        lx("Translate and channel-filter", "Control gain", "Baseband samples", "Baseband samples");
        lx("Control gain", "Recover timing (PFB)", "Normalised samples", "Baseband samples");
        lx("Recover timing (PFB)", "Correlate unique word", "SymbolChunk (q_sym)", "Symbols");
        lx("Correlate unique word", "Extract segment", "Frame start", "Segments");
        lx("Extract segment", "Estimate and remove CFO", "Segment", "Segments");
        lx("Estimate and remove CFO", "Equalise channel (LLS)", "De-rotated segment", "Segments");
        lx("Equalise channel (LLS)", "Slice 256-QAM", "Frame (q_frames)", "Payload symbols");
        lx("Slice 256-QAM", "Decode Reed-Solomon", "Codeword bytes", "Codewords");
        lx("Decode Reed-Solomon", "Compare with ground truth", "Decoded payload", "Decoded payload");
        lx("Compare with ground truth", "Aggregate metrics", "Frame counters", "Frame statistics");
        lx("Estimate SNR", "Aggregate metrics", "SNR value", "SNR estimate");
        lx("Aggregate metrics", "Write console, CSV and exit code", "Interval metrics", "Frame statistics");
        lx("Write console, CSV and exit code", "Monitor results", "Console report", "Console report");
        lx("Write console, CSV and exit code", "Store files", "CSV log", "CSV log");
        lx("Write console, CSV and exit code", "Check exit status", "Exit status", "Exit status");
        lx("Aggregate metrics", "Serve dashboard API", "History point", "Metrics snapshot");
        lx("Equalise channel (LLS)", "Serve dashboard API", "Constellation and taps", "Metrics snapshot");
        lx("Estimate spectrum", "Serve dashboard API", "Spectrum", "Metrics snapshot");
        lx("Serve dashboard API", "Render dashboard", "Dashboard data", "Dashboard data");
        lx("Render dashboard", "Validate control request", "Control request", "Control request");
        lx("Validate control request", "Apply source control", "Validated control", "Control request");
        lx("Apply source control", "Retune radio", "Tuning command", "Tuning command");
        lx("Apply source control", "Generate simulated signal", "Channel settings", "Channel settings");

        // Component exchanges: internal ones are the SPSC queues and shared state
        List<ComponentExchange> in = lSystem.getOwnedComponentExchanges();
        List<ComponentExchange> ext = pkg.getOwnedComponentExchanges();
        lceAdd(in, lSource, lIngest, "SampleSource::read", "Virtual read() into the q_in slot.", null, "USRP samples", "Replayed samples", "Simulated samples");
        lceAdd(in, lIngest, lFront, "q_in (SPSC, 128 slots)", "Lock-free ring of SampleChunk (8192 x cf32); about 750 ms of input.", null, "SampleChunk (q_in)");
        lceAdd(in, lFront, lSync, "q_sym (SPSC, 128 slots)", "Lock-free ring of SymbolChunk (up to 4096 symbols).", null, "SymbolChunk (q_sym)");
        lceAdd(in, lSync, lDecode, "q_frames (SPSC, 1024 slots)", "Lock-free ring of Frame (216 symbols + EqualizerResult).", null, "Frame (q_frames)");
        lceAdd(in, lFront, lRadio, "q_snr (SPSC, 64 slots)", "Copy of the DC-blocked stream; dropped (never blocks) when full.", null, "SampleChunk (q_snr)", "Spectrum input");
        lceAdd(in, lDecode, lDispatch, "Metrics (decoder)", "Relaxed atomic counters.", null, "Frame counters");
        lceAdd(in, lRadio, lDispatch, "Metrics (radiometer)", "Relaxed atomic SNR value.", null, "SNR value");
        lceAdd(in, lDispatch, lDash, "UiState history", "Mutex-protected history ring (600 points).", null, "History point");
        lceAdd(in, lSync, lDash, "UiState constellation", "Published with try_lock only; never blocks T3.", null, "Constellation and taps");
        lceAdd(in, lRadio, lDash, "UiState spectrum", "Published with try_lock only; never blocks T5.", null, "Spectrum");
        lceAdd(in, lDash, lSource, "Runtime control", "SampleSource::set_center_freq / set_gain / set_sim_esn0 / set_sim_cfo.", null, "Validated control");
        lceAdd(ext, lActors.get("AIW Transmitter"), lActors.get("USRP Radio"), "RF link", null, sce.get("RF link"), "RF waveform");
        lceAdd(ext, lActors.get("USRP Radio"), lSource, "UHD sample stream", "fc32 over sc16 wire format.", sce.get("Sample stream"), "IQ stream (fc32)");
        lceAdd(ext, lSource, lActors.get("USRP Radio"), "UHD control", null, sce.get("Radio control"), "Tuning command");
        lceAdd(ext, lActors.get("Test Operator"), lIngest, "Command line", null, sce.get("Command line"), "Run configuration");
        lceAdd(ext, lDispatch, lActors.get("Test Operator"), "Console", null, sce.get("Console"), "Console report");
        lceAdd(ext, lDash, lActors.get("Web Browser"), "Dashboard HTTP", null, sce.get("HTTP out"), "Dashboard data");
        lceAdd(ext, lActors.get("Web Browser"), lDash, "Control POST", null, sce.get("HTTP in"), "Control request");
        lceAdd(ext, lActors.get("File System"), lSource, "Capture input", null, sce.get("File read"), "Capture file");
        lceAdd(ext, lDispatch, lActors.get("File System"), "CSV output", null, sce.get("File write"), "CSV log");
        lceAdd(ext, lDispatch, lActors.get("Test Automation"), "Process exit code", null, sce.get("Exit code"), "Exit status");

        Startup.log("LA->SA exchange realizations: FE " + autoRealizeFE(lfe.values(), sfe.values()) + " of "
                + lfe.size() + ", CE " + autoRealizeCE(lce.values(), sce.values()) + " of " + lce.size());
        buildDataModel();

        // Capability realizations
        CapabilityRealizationPkg crp = (CapabilityRealizationPkg) la.getOwnedAbstractCapabilityPkg();
        for (Capability c : caps) {
            CapabilityRealization cr = n(LA.createCapabilityRealization(), c.getName(), null);
            cr.setDescription(c.getDescription());
            crp.getOwnedCapabilityRealizations().add(cr);
            AbstractCapabilityRealization r = InteractionFactory.eINSTANCE.createAbstractCapabilityRealization();
            r.setSourceElement(cr);
            r.setTargetElement(c);
            cr.getOwnedAbstractCapabilityRealizations().add(r);
        }

        List<EObject> lab = new ArrayList<>(partsOf(lSystem));
        lab.addAll(partsOf(lIngest, lFront, lSync, lDecode, lRadio, lDispatch, lDash, lSource));
        for (LogicalComponent a : lActors.values()) lab.addAll(a.getRepresentingParts());
        lab.addAll(lce.values());
        diagrams.add(new DiagramSpec("Logical Architecture Blank", lSystem, "[LAB] AIW-Rx Pipeline Components and Queues", "ab",
                new ArrayList<>(lab)));
        diagrams.add(new DiagramSpec("Logical Architecture Blank", lSystem, "[LAB] AIW-Rx Logical Architecture (with functions)", "ab", lab)
                .functions(lf.values()));
        List<EObject> ldf = new ArrayList<>(lf.values());
        ldf.addAll(lfe.values());
        diagrams.add(new DiagramSpec("Logical Data Flow Blank", lRoot, "[LDFB] Logical Functions", "df", ldf));
    }

    LogicalComponent lc(String name, String desc) {
        LogicalComponent c = n(LA.createLogicalComponent(), name, desc);
        lSystem.getOwnedLogicalComponents().add(c);
        partIn(lSystem, c);
        return c;
    }

    void lfn(LogicalComponent owner, String name, String realizes, String desc) {
        LogicalFunction f = fn(lRoot, LA.createLogicalFunction(), name, desc);
        alloc(owner, f);
        realizeFn(f, sf.get(realizes));
        lf.put(name, f);
        lAlloc.put(f, owner);
    }

    void lx(String from, String to, String name, String realizes) {
        FunctionalExchange x = fe(lRoot, lf.get(from), lf.get(to), name, null);
        lfe.put(name, x);
    }

    void lceAdd(List<ComponentExchange> owner, LogicalComponent from, LogicalComponent to, String name, String desc,
            ComponentExchange realizes, String... fes) {
        FunctionalExchange[] a = new FunctionalExchange[fes.length];
        for (int i = 0; i < fes.length; i++) a[i] = lfe.get(fes[i]);
        ComponentExchange x = ce(owner, from, to, name, desc, a);
        lce.put(name, x);
    }

    void buildDataModel() {
        DataPkg dp = la.getOwnedDataPkg();
        dp.setDescription("<p>Pipeline records carried by the queues (include/receiver.hpp, equalizer.hpp).</p>");
        DataPkg predefined = sa.getOwnedDataPkg().getOwnedDataPkgs().get(0);
        Map<String, DataType> t = new LinkedHashMap<>();
        for (DataType d : predefined.getOwnedDataTypes()) t.put(d.getName(), d);
        InformationFactory IF = InformationFactory.eINSTANCE;

        org.polarsys.capella.core.data.information.Class sample = cls(dp, "SampleChunk", "8192 complex-float32 samples read in one call.");
        card(attr(sample, "data", t.get("Float"), "cf32[8192] interleaved I/Q"), "16384", "16384");
        attr(sample, "n", t.get("UnsignedLong"), "valid samples");
        attr(sample, "t_ingest", t.get("LongLong"), "steady_clock timestamp at read");
        org.polarsys.capella.core.data.information.Class symbol = cls(dp, "SymbolChunk", "Up to 4096 symbols at 1 sample/symbol.");
        card(attr(symbol, "data", t.get("Float"), "cf32[4096]"), "8192", "8192");
        attr(symbol, "n", t.get("UnsignedLong"), null);
        attr(symbol, "t_ingest", t.get("LongLong"), null);
        org.polarsys.capella.core.data.information.Class eqr = cls(dp, "EqualizerResult", "Output of the two-pass equaliser.");
        card(attr(eqr, "data", t.get("Float"), "cf32[216] corrected payload symbols"), "432", "432");
        card(attr(eqr, "taps", t.get("Float"), "cf32[5] equaliser taps"), "10", "10");
        attr(eqr, "cfo_rad_per_sym", t.get("Double"), null);
        attr(eqr, "residual_phase", t.get("Double"), null);
        attr(eqr, "uw_mer_db", t.get("Double"), null);
        attr(eqr, "cond_estimate", t.get("Double"), null);
        org.polarsys.capella.core.data.information.Class frame = cls(dp, "Frame", "One demultiplexed frame for T4.");
        // Frame --(composition)--> EqualizerResult, modelled as an association.
        Property eqp = attr(frame, "eq", null, "Equaliser output for this frame.");
        eqp.setAbstractType(eqr);
        eqp.setAggregationKind(org.polarsys.capella.core.data.information.AggregationKind.COMPOSITION);
        org.polarsys.capella.core.data.information.Association as = InformationFactory.eINSTANCE.createAssociation();
        as.setName("Frame owns EqualizerResult");
        Property back = n(InformationFactory.eINSTANCE.createProperty(), "frame", null);
        back.setAbstractType(frame);
        back.setAggregationKind(org.polarsys.capella.core.data.information.AggregationKind.ASSOCIATION);
        card(back, "1", "1");
        as.getOwnedMembers().add(back);
        as.getNavigableMembers().add(eqp);
        dp.getOwnedAssociations().add(as);
        attr(frame, "corr_metric", t.get("Float"), null);
        attr(frame, "stream_index", t.get("UnsignedLongLong"), null);
        attr(frame, "t_ingest", t.get("LongLong"), null);
        org.polarsys.capella.core.data.information.Class metrics = cls(dp, "Metrics", "Relaxed atomic counters shared by all threads (include/metrics.hpp).");
        for (String s : List.of("samples_in", "frames_decoded", "frames_uncorrectable", "pre_fec_bit_errors",
                "post_fec_bit_errors", "chunks_dropped", "usrp_overflows"))
            attr(metrics, s, t.get("UnsignedLongLong"), null);
        for (String s : List.of("snr_db", "uw_mer_db", "cfo_hz", "agc_gain", "pfb_rate"))
            attr(metrics, s, t.get("Double"), null);
        org.polarsys.capella.core.data.information.Class hp = cls(dp, "HistoryPoint", "One dashboard trend point per report interval.");
        for (String s : List.of("t", "frames_per_s", "pre_ber", "post_ber", "snr_db", "mer_db", "cfo_hz", "lat_mean_ms", "lat_max_ms"))
            attr(hp, s, t.get("Double"), null);

        ExchangeItem eiSample = ei(dp, "SampleChunk", sample);
        ExchangeItem eiSymbol = ei(dp, "SymbolChunk", symbol);
        ExchangeItem eiFrame = ei(dp, "Frame", frame);
        ExchangeItem eiMetrics = ei(dp, "Metrics", metrics);
        ExchangeItem eiHistory = ei(dp, "HistoryPoint", hp);
        convoy(eiSample, lfe.get("SampleChunk (q_in)"), lfe.get("SampleChunk (q_snr)"), lfe.get("USRP samples"),
                lfe.get("Replayed samples"), lfe.get("Simulated samples"));
        convoy(eiSymbol, lfe.get("SymbolChunk (q_sym)"));
        convoy(eiFrame, lfe.get("Frame (q_frames)"));
        convoy(eiMetrics, lfe.get("Frame counters"), lfe.get("SNR value"), lfe.get("Interval metrics"));
        convoy(eiHistory, lfe.get("History point"));
        lce.get("q_in (SPSC, 128 slots)").getConvoyedInformations().add(eiSample);
        lce.get("q_snr (SPSC, 64 slots)").getConvoyedInformations().add(eiSample);
        lce.get("q_sym (SPSC, 128 slots)").getConvoyedInformations().add(eiSymbol);
        lce.get("q_frames (SPSC, 1024 slots)").getConvoyedInformations().add(eiFrame);
        lce.get("Metrics (decoder)").getConvoyedInformations().add(eiMetrics);
        lce.get("Metrics (radiometer)").getConvoyedInformations().add(eiMetrics);
        lce.get("UiState history").getConvoyedInformations().add(eiHistory);

        diagrams.add(new DiagramSpec("Class Diagram Blank", dp, "[CDB] Pipeline Data Model", "cdb",
                List.of(sample, symbol, eqr, frame, metrics, hp)));
    }

    org.polarsys.capella.core.data.information.Class cls(DataPkg dp, String name, String desc) {
        var c = n(InformationFactory.eINSTANCE.createClass(), name, desc);
        dp.getOwnedClasses().add(c);
        return c;
    }

    Property attr(org.polarsys.capella.core.data.information.Class c, String name, DataType type, String desc) {
        Property p = n(InformationFactory.eINSTANCE.createProperty(), name, desc);
        if (type != null) p.setAbstractType(type);
        card(p, "1", "1");
        c.getOwnedFeatures().add(p);
        return p;
    }

    static void card(org.polarsys.capella.core.data.information.MultiplicityElement m, String min, String max) {
        var f = org.polarsys.capella.core.data.information.datavalue.DatavalueFactory.eINSTANCE;
        var lo = f.createLiteralNumericValue();
        lo.setValue(min);
        m.setOwnedMinCard(lo);
        var hi = f.createLiteralNumericValue();
        hi.setValue(max);
        m.setOwnedMaxCard(hi);
    }

    ExchangeItem ei(DataPkg dp, String name, org.polarsys.capella.core.data.information.Class type) {
        ExchangeItem e = n(InformationFactory.eINSTANCE.createExchangeItem(), name, null);
        e.setExchangeMechanism(ExchangeMechanism.FLOW);
        ExchangeItemElement el = InformationFactory.eINSTANCE.createExchangeItemElement();
        el.setName(name.toLowerCase());
        el.setAbstractType(type);
        el.setDirection(org.polarsys.capella.core.data.information.ParameterDirection.UNSET);
        card(el, "1", "1");
        e.getOwnedElements().add(el);
        dp.getOwnedExchangeItems().add(e);
        return e;
    }

    // =====================================================================================
    // Physical Architecture
    // =====================================================================================
    PhysicalComponent pSystem, pHost, pUsrp;
    Map<LogicalComponent, PhysicalComponent> behaviour = new LinkedHashMap<>();
    Map<String, PhysicalComponent> pActors = new LinkedHashMap<>();
    Map<LogicalFunction, PhysicalFunction> pf = new LinkedHashMap<>();
    Map<String, FunctionalExchange> pfe = new LinkedHashMap<>();
    Map<String, ComponentExchange> pce = new LinkedHashMap<>();

    void buildPA() {
        pa.setDescription("<p>Deployment of AIW-Rx: one aiw_rx process whose threads run on the host PC, "
                + "fed by a USRP over USB 3 or Gigabit Ethernet.</p>");
        PhysicalComponentPkg pkg = pa.getOwnedPhysicalComponentPkg();
        pSystem = (PhysicalComponent) pa.getSystem();
        n(pSystem, "AIW-Rx Deployment", "Physical realisation of AIW-Rx.");
        for (Part p : pSystem.getRepresentingParts()) p.setName(pSystem.getName());

        pHost = node(pSystem, "Host PC", "x86-64 PC, 4 cores recommended; Ubuntu 24.04 (glibc 2.38+) or Windows 10/11.");
        prop(pHost, "Operating system", "Ubuntu 24.04 LTS / Windows 10-11");

        // Actors: the USRP is a hardware node outside the software system
        for (LogicalComponent a : lActors.values()) {
            PhysicalComponent p = n(PA.createPhysicalComponent(), a.getName(), null);
            p.setDescription(a.getDescription());
            p.setActor(true);
            p.setHuman(a.isHuman());
            p.setNature(a.getName().equals("USRP Radio") || a.getName().equals("AIW Transmitter")
                    ? PhysicalComponentNature.NODE : PhysicalComponentNature.BEHAVIOR);
            pkg.getOwnedPhysicalComponents().add(p);
            partIn(pkg, p);
            realizeComp(p, a);
            pActors.put(a.getName(), p);
        }
        pUsrp = pActors.get("USRP Radio");
        prop(pUsrp, "Device", "Ettus USRP B2xx, serial 3273A14, antenna RX2");

        String[][] threads = { { "Sample Ingestion (T1)", "T1 ingestion thread" }, { "Front End (T2)", "T2 front-end thread" },
                { "Frame Synchroniser and Equaliser (T3)", "T3 sync/EQ thread" }, { "Decoder (T4)", "T4 decode thread" },
                { "Radiometer (T5)", "T5 radiometer thread" }, { "Metrics Dispatcher", "Main thread (dispatcher)" },
                { "Dashboard Server", "HTTP server thread" }, { "Sample Source Adapter", "Source adapter (UHD / file / simulator)" } };
        Part hostPart = pHost.getRepresentingParts().get(0);
        for (String[] th : threads) {
            LogicalComponent l = null;
            for (LogicalComponent c : List.of(lIngest, lFront, lSync, lDecode, lRadio, lDispatch, lDash, lSource))
                if (c.getName().equals(th[0])) l = c;
            PhysicalComponent b = n(PA.createPhysicalComponent(), th[1], "Runs inside the aiw_rx process; realises " + th[0] + ".");
            b.setNature(PhysicalComponentNature.BEHAVIOR);
            pSystem.getOwnedPhysicalComponents().add(b);
            Part bp = partIn(pSystem, b);
            PartDeploymentLink d = DeploymentFactory.eINSTANCE.createPartDeploymentLink();
            d.setLocation(hostPart);
            d.setDeployedElement(bp);
            hostPart.getOwnedDeploymentLinks().add(d);
            realizeComp(b, l);
            behaviour.put(l, b);
        }
        PhysicalComponent uhd = n(PA.createPhysicalComponent(), "UHD driver (libuhd 4.6)", "Ettus USRP Hardware Driver, linked into aiw_rx.");
        uhd.setNature(PhysicalComponentNature.BEHAVIOR);
        pSystem.getOwnedPhysicalComponents().add(uhd);
        Part uhdPart = partIn(pSystem, uhd);
        PartDeploymentLink d = DeploymentFactory.eINSTANCE.createPartDeploymentLink();
        d.setLocation(hostPart);
        d.setDeployedElement(uhdPart);
        hostPart.getOwnedDeploymentLinks().add(d);

        // Physical functions: one per logical function, same allocation
        PhysicalFunction pRoot = (PhysicalFunction) ((PhysicalFunctionPkg) pa.getOwnedFunctionPkg())
                .getOwnedPhysicalFunctions().get(0);
        for (var e : lf.entrySet()) {
            LogicalFunction l = e.getValue();
            PhysicalFunction p = fn(pRoot, PA.createPhysicalFunction(), l.getName(), null);
            p.setDescription(l.getDescription());
            realizeFn(p, l);
            LogicalComponent owner = lAlloc.get(l);
            PhysicalComponent pc = behaviour.containsKey(owner) ? behaviour.get(owner) : pActors.get(owner.getName());
            alloc(pc, p);
            pf.put(l, p);
        }
        for (var e : lfe.entrySet()) {
            FunctionalExchange l = e.getValue();
            AbstractFunction from = (AbstractFunction) l.getSource().eContainer();
            AbstractFunction to = (AbstractFunction) l.getTarget().eContainer();
            FunctionalExchange p = fe(pRoot, pf.get(from), pf.get(to), l.getName(), null);
            realizeFE(p, l);
            realizePort(p.getSource(), l.getSource());
            realizePort(p.getTarget(), l.getTarget());
            for (var ei : l.getExchangedItems()) {
                ((org.polarsys.capella.core.data.fa.FunctionOutputPort) p.getSource()).getOutgoingExchangeItems().add(ei);
                ((org.polarsys.capella.core.data.fa.FunctionInputPort) p.getTarget()).getIncomingExchangeItems().add(ei);
            }
            p.getExchangedItems().addAll(l.getExchangedItems());
            pfe.put(e.getKey(), p);
        }
        // Component exchanges: mirror the logical ones between the realising components
        for (var e : lce.entrySet()) {
            ComponentExchange l = e.getValue();
            Component from = (Component) l.getSource().eContainer();
            Component to = (Component) l.getTarget().eContainer();
            PhysicalComponent pfrom = behaviour.containsKey(from) ? behaviour.get(from) : pActors.get(from.getName());
            PhysicalComponent pto = behaviour.containsKey(to) ? behaviour.get(to) : pActors.get(to.getName());
            List<FunctionalExchange> allocated = new ArrayList<>();
            for (FunctionalExchange f : l.getAllocatedFunctionalExchanges())
                for (var k : lfe.entrySet())
                    if (k.getValue() == f) allocated.add(pfe.get(k.getKey()));
            boolean internal = !pfrom.isActor() && !pto.isActor();
            ComponentExchange p = ce(internal ? pSystem.getOwnedComponentExchanges() : pkg.getOwnedComponentExchanges(),
                    pfrom, pto, l.getName(), null, allocated.toArray(new FunctionalExchange[0]));
            p.setDescription(l.getDescription());
            p.getConvoyedInformations().addAll(l.getConvoyedInformations());
            realizeCE(p, l);
            realizePort(p.getSource(), l.getSource());
            realizePort(p.getTarget(), l.getTarget());
            pce.put(e.getKey(), p);
        }
        // Physical links
        List<org.polarsys.capella.core.data.cs.PhysicalLink> pl = pkg.getOwnedPhysicalLinks();
        plink(pl, pActors.get("AIW Transmitter"), pUsrp, "RF path", "Antenna or cable with attenuator, 917 MHz.", pce.get("RF link"));
        plink(pl, pUsrp, pHost, "USB 3.0 / GbE", "USRP transport: USB 3.0 (B2xx) or Gigabit Ethernet (N2xx/X3xx).",
                pce.get("UHD sample stream"), pce.get("UHD control"));

        CapabilityRealizationPkg pcrp = (CapabilityRealizationPkg) pa.getOwnedAbstractCapabilityPkg();
        for (CapabilityRealization lcr : ((CapabilityRealizationPkg) la.getOwnedAbstractCapabilityPkg()).getOwnedCapabilityRealizations()) {
            CapabilityRealization cr = n(LA.createCapabilityRealization(), lcr.getName(), null);
            cr.setDescription(lcr.getDescription());
            pcrp.getOwnedCapabilityRealizations().add(cr);
            AbstractCapabilityRealization r = InteractionFactory.eINSTANCE.createAbstractCapabilityRealization();
            r.setSourceElement(cr);
            r.setTargetElement(lcr);
            cr.getOwnedAbstractCapabilityRealizations().add(r);
        }
        List<EObject> pab = new ArrayList<>(partsOf(pHost));
        for (PhysicalComponent a : pActors.values()) pab.addAll(a.getRepresentingParts());
        DiagramSpec pabSpec = new DiagramSpec("Physical Architecture Blank", pSystem, "[PAB] AIW-Rx Deployment", "deploy", pab);
        pabSpec.links = new ArrayList<>(pl);
        pabSpec.deployHost = hostPart;
        List<EObject> dep = new ArrayList<>();
        for (PhysicalComponent b : behaviour.values()) dep.addAll(b.getRepresentingParts());
        dep.add(uhdPart);
        pabSpec.deployed = dep;
        diagrams.add(pabSpec);
        List<EObject> pbeh = new ArrayList<>(partsOf(pSystem));
        for (PhysicalComponent b : behaviour.values()) pbeh.addAll(b.getRepresentingParts());
        for (PhysicalComponent a : pActors.values()) if (a.getNature() == PhysicalComponentNature.BEHAVIOR || a == pUsrp) pbeh.addAll(a.getRepresentingParts());
        pbeh.addAll(pce.values());
        pbeh.removeIf(e -> false);
        diagrams.add(new DiagramSpec("Physical Architecture Blank", pSystem, "[PAB] AIW-Rx Behaviour Components and Exchanges", "ab", pbeh));
    }

    PhysicalComponent node(PhysicalComponent parent, String name, String desc) {
        PhysicalComponent c = n(PA.createPhysicalComponent(), name, desc);
        c.setNature(PhysicalComponentNature.NODE);
        parent.getOwnedPhysicalComponents().add(c);
        partIn(parent, c);
        return c;
    }

    // =====================================================================================
    // EPBS
    // =====================================================================================
    void buildEPBS() {
        epbs.setDescription("<p>Configuration items delivered for AIW-Rx.</p>");
        ConfigurationItemPkg pkg = epbs.getOwnedConfigurationItemPkg();
        ConfigurationItem root = pkg.getOwnedConfigurationItems().get(0);
        n(root, "AIW-Rx System", "Everything delivered for AIW-Rx.");
        root.setKind(ConfigurationItemKind.SYSTEM_CI);
        for (Part p : root.getRepresentingParts()) p.setName(root.getName());

        ConfigurationItem sw = ci(root, "AIW-Rx Software (CSCI)", ConfigurationItemKind.CSCI, "aiw_receiver/ source tree, CMake build, C++20.");
        ConfigurationItem exe = ci(sw, "aiw_rx executable", ConfigurationItemKind.CSCI, "src/main.cpp + aiw_core; Ubuntu (UHD) and Windows (MinGW, no UHD) builds.");
        for (PhysicalComponent b : behaviour.values()) artifact(exe, b);
        ci(sw, "aiw_core library", ConfigurationItemKind.CSCI, "Static library with all DSP, pipeline, HTTP server and dashboard code.");
        ci(sw, "Dashboard page", ConfigurationItemKind.CSCI, "web/index.html, embedded into aiw_rx at build time.");
        ci(sw, "aiw_tests", ConfigurationItemKind.CSCI, "Offline verification suite (2408 checks) and CTest end-to-end run.");
        ci(sw, "Documentation", ConfigurationItemKind.NDICI, "README, Installation guide, User manual, Architecture.");
        ConfigurationItem uhd = ci(root, "UHD 4.6 (COTS)", ConfigurationItemKind.COTSCI, "Ettus USRP Hardware Driver, libuhd4.6.0t64 / libuhd-dev.");
        for (PhysicalComponent c : pSystem.getOwnedPhysicalComponents())
            if (c.getName().startsWith("UHD")) artifact(uhd, c);
        ci(root, "Eigen 3.4 (COTS)", ConfigurationItemKind.COTSCI, "Header-only linear algebra (equaliser least-squares solve).");
        ConfigurationItem hw = ci(root, "USRP B2xx (HWCI)", ConfigurationItemKind.HWCI, "Ettus USRP, serial 3273A14.");
        artifact(hw, pUsrp);
        ConfigurationItem host = ci(root, "Host PC (HWCI)", ConfigurationItemKind.HWCI, "x86-64 PC running Ubuntu 24.04 or Windows.");
        artifact(host, pHost);

        DiagramSpec eab = new DiagramSpec("EPBS Architecture Blank", root, "[EAB] AIW-Rx Configuration Items", "nested", List.of());
        eab.nestedRoot = root;
        diagrams.add(eab);
    }

    List<EObject> collectParts(ConfigurationItem c) {
        List<EObject> l = new ArrayList<>(c.getRepresentingParts());
        for (ConfigurationItem x : c.getOwnedConfigurationItems()) l.addAll(collectParts(x));
        return l;
    }

    ConfigurationItem ci(ConfigurationItem parent, String name, ConfigurationItemKind kind, String desc) {
        ConfigurationItem c = n(EpbsFactory.eINSTANCE.createConfigurationItem(), name, desc);
        c.setKind(kind);
        parent.getOwnedConfigurationItems().add(c);
        partIn(parent, c);
        return c;
    }

    void artifact(ConfigurationItem ci, PhysicalComponent pc) {
        PhysicalArtifactRealization r = EpbsFactory.eINSTANCE.createPhysicalArtifactRealization();
        r.setSourceElement(ci);
        r.setTargetElement(pc);
        ci.getOwnedPhysicalArtifactRealizations().add(r);
    }
}
