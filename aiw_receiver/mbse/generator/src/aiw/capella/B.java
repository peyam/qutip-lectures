package aiw.capella;

import java.util.List;

import org.polarsys.capella.common.data.modellingcore.AbstractNamedElement;
import org.polarsys.capella.common.data.modellingcore.AbstractType;
import org.polarsys.capella.core.data.capellacore.CapellaElement;
import org.polarsys.capella.core.data.capellacore.CapellacoreFactory;
import org.polarsys.capella.core.data.capellacore.StringPropertyValue;
import org.polarsys.capella.core.data.cs.Component;
import org.polarsys.capella.core.data.cs.ComponentPkg;
import org.polarsys.capella.core.data.cs.ComponentRealization;
import org.polarsys.capella.core.data.cs.CsFactory;
import org.polarsys.capella.core.data.cs.Part;
import org.polarsys.capella.core.data.cs.PhysicalLink;
import org.polarsys.capella.core.data.cs.PhysicalPort;
import org.polarsys.capella.core.data.fa.AbstractFunction;
import org.polarsys.capella.core.data.fa.ComponentExchange;
import org.polarsys.capella.core.data.fa.ComponentExchangeAllocation;
import org.polarsys.capella.core.data.fa.ComponentExchangeFunctionalExchangeAllocation;
import org.polarsys.capella.core.data.fa.ComponentExchangeKind;
import org.polarsys.capella.core.data.fa.ComponentExchangeRealization;
import org.polarsys.capella.core.data.fa.ComponentFunctionalAllocation;
import org.polarsys.capella.core.data.fa.ComponentPort;
import org.polarsys.capella.core.data.fa.ComponentPortKind;
import org.polarsys.capella.core.data.fa.FaFactory;
import org.polarsys.capella.core.data.fa.FunctionInputPort;
import org.polarsys.capella.core.data.fa.FunctionOutputPort;
import org.polarsys.capella.core.data.fa.FunctionRealization;
import org.polarsys.capella.core.data.fa.FunctionalChain;
import org.polarsys.capella.core.data.fa.FunctionalChainInvolvementFunction;
import org.polarsys.capella.core.data.fa.FunctionalChainInvolvementLink;
import org.polarsys.capella.core.data.fa.FunctionalExchange;
import org.polarsys.capella.core.data.fa.FunctionalExchangeRealization;
import org.polarsys.capella.core.data.fa.OrientationPortKind;
import org.polarsys.capella.core.data.information.ExchangeItem;

/** Small helpers over Capella's EMF factories. */
final class B {
    private B() {
    }

    static final FaFactory FA = FaFactory.eINSTANCE;
    static final CsFactory CS = CsFactory.eINSTANCE;

    static String esc(String s) {
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
    }

    /** Name + HTML description. */
    static <T extends CapellaElement> T n(T e, String name, String desc) {
        ((AbstractNamedElement) e).setName(name);
        if (desc != null && !desc.isEmpty()) e.setDescription("<p>" + esc(desc) + "</p>");
        return e;
    }

    static void prop(CapellaElement e, String name, String value) {
        StringPropertyValue v = CapellacoreFactory.eINSTANCE.createStringPropertyValue();
        v.setName(name);
        v.setValue(value);
        e.getOwnedPropertyValues().add(v);
    }

    static <F extends AbstractFunction> F fn(AbstractFunction parent, F f, String name, String desc) {
        parent.getOwnedFunctions().add(f);
        return n(f, name, desc);
    }

    static void alloc(Component c, AbstractFunction... fns) {
        for (AbstractFunction f : fns) {
            ComponentFunctionalAllocation a = FA.createComponentFunctionalAllocation();
            a.setSourceElement(c);
            a.setTargetElement(f);
            c.getOwnedFunctionalAllocation().add(a);
        }
    }

    static Part partIn(ComponentPkg pkg, Component c) {
        Part p = CS.createPart();
        p.setName(c.getName());
        p.setAbstractType((AbstractType) c);
        pkg.getOwnedParts().add(p);
        return p;
    }

    static Part partIn(Component parent, Component c) {
        Part p = CS.createPart();
        p.setName(c.getName());
        p.setAbstractType((AbstractType) c);
        parent.getOwnedFeatures().add(p);
        return p;
    }

    /** Functional exchange owned by {@code owner}, with new ports on both functions. */
    static FunctionalExchange fe(AbstractFunction owner, AbstractFunction from, AbstractFunction to, String name,
            String desc) {
        FunctionOutputPort o = FA.createFunctionOutputPort();
        o.setName(name);
        from.getOutputs().add(o);
        FunctionInputPort i = FA.createFunctionInputPort();
        i.setName(name);
        to.getInputs().add(i);
        FunctionalExchange x = FA.createFunctionalExchange();
        n(x, name, desc);
        x.setSource(o);
        x.setTarget(i);
        owner.getOwnedFunctionalExchanges().add(x);
        return x;
    }

    static ComponentPort cport(Component c, String name, OrientationPortKind dir) {
        ComponentPort p = FA.createComponentPort();
        p.setName(name);
        p.setKind(ComponentPortKind.FLOW);
        p.setOrientation(dir);
        c.getOwnedFeatures().add(p);
        return p;
    }

    /** Component exchange from -> to (new flow ports), allocating the given functional exchanges. */
    static ComponentExchange ce(List<ComponentExchange> owner, Component from, Component to, String name, String desc,
            FunctionalExchange... fes) {
        ComponentExchange x = FA.createComponentExchange();
        n(x, name, desc);
        x.setKind(ComponentExchangeKind.FLOW);
        x.setSource(cport(from, name, OrientationPortKind.OUT));
        x.setTarget(cport(to, name, OrientationPortKind.IN));
        owner.add(x);
        allocFE(x, fes);
        return x;
    }

    static void allocFE(ComponentExchange x, FunctionalExchange... fes) {
        for (FunctionalExchange f : fes) {
            ComponentExchangeFunctionalExchangeAllocation a = FA.createComponentExchangeFunctionalExchangeAllocation();
            a.setSourceElement(x);
            a.setTargetElement(f);
            x.getOwnedComponentExchangeFunctionalExchangeAllocations().add(a);
        }
    }

    static void convoy(ExchangeItem ei, FunctionalExchange... fes) {
        for (FunctionalExchange f : fes) {
            f.getExchangedItems().add(ei);
            if (f.getSource() instanceof FunctionOutputPort o) o.getOutgoingExchangeItems().add(ei);
            if (f.getTarget() instanceof FunctionInputPort i) i.getIncomingExchangeItems().add(ei);
        }
    }

    static void realizePort(Object lower, Object upper) {
        if (!(lower instanceof org.polarsys.capella.core.data.information.Port lp)
                || !(upper instanceof org.polarsys.capella.core.data.information.Port up)) return;
        org.polarsys.capella.core.data.information.PortRealization r =
                org.polarsys.capella.core.data.information.InformationFactory.eINSTANCE.createPortRealization();
        r.setSourceElement(lp);
        r.setTargetElement(up);
        lp.getOwnedPortRealizations().add(r);
    }

    /** True if {@code lower} (or a function containing it) realizes {@code upper}. */
    static boolean realizesFn(org.eclipse.emf.ecore.EObject lower, org.eclipse.emf.ecore.EObject upper) {
        for (org.eclipse.emf.ecore.EObject f = lower; f instanceof AbstractFunction af; f = f.eContainer())
            for (FunctionRealization r : af.getOwnedFunctionRealizations())
                if (r.getTargetElement() == upper) return true;
        return false;
    }

    /** True if {@code lower} (or a component containing it) realizes {@code upper}. */
    static boolean realizesComp(org.eclipse.emf.ecore.EObject lower, org.eclipse.emf.ecore.EObject upper) {
        for (org.eclipse.emf.ecore.EObject c = lower; c instanceof Component cc; c = c.eContainer()) {
            if (cc == upper) return true;
            for (ComponentRealization r : cc.getOwnedComponentRealizations())
                if (r.getTargetElement() == upper) return true;
        }
        return false;
    }

    static org.eclipse.emf.ecore.EObject ownerOf(org.eclipse.emf.ecore.EObject end) {
        return end instanceof org.polarsys.capella.core.data.information.Port ? end.eContainer() : end;
    }

    /** Realize each lower exchange by the first upper exchange whose two ends it traces to (plus port realizations). */
    static int autoRealizeFE(java.util.Collection<FunctionalExchange> lower, java.util.Collection<FunctionalExchange> upper) {
        int n = 0;
        for (FunctionalExchange l : lower)
            for (FunctionalExchange u : upper)
                if (realizesFn(ownerOf(l.getSource()), ownerOf(u.getSource()))
                        && realizesFn(ownerOf(l.getTarget()), ownerOf(u.getTarget()))) {
                    realizeFE(l, u);
                    realizePort(l.getSource(), u.getSource());
                    realizePort(l.getTarget(), u.getTarget());
                    n++;
                    break;
                }
        return n;
    }

    static int autoRealizeCE(java.util.Collection<? extends ComponentExchange> lower,
            java.util.Collection<? extends ComponentExchange> upper) {
        int n = 0;
        for (ComponentExchange l : lower)
            for (ComponentExchange u : upper)
                if (realizesComp(ownerOf(l.getSource()), ownerOf(u.getSource()))
                        && realizesComp(ownerOf(l.getTarget()), ownerOf(u.getTarget()))) {
                    realizeCE(l, u);
                    realizePort(l.getSource(), u.getSource());
                    realizePort(l.getTarget(), u.getTarget());
                    n++;
                    break;
                }
        return n;
    }

    static void realizeFn(AbstractFunction lower, AbstractFunction upper) {
        FunctionRealization r = FA.createFunctionRealization();
        r.setSourceElement(lower);
        r.setTargetElement(upper);
        lower.getOwnedFunctionRealizations().add(r);
    }

    static void realizeFE(FunctionalExchange lower, FunctionalExchange upper) {
        FunctionalExchangeRealization r = FA.createFunctionalExchangeRealization();
        r.setSourceElement(lower);
        r.setTargetElement(upper);
        lower.getOwnedFunctionalExchangeRealizations().add(r);
    }

    static void realizeCE(ComponentExchange lower, ComponentExchange upper) {
        ComponentExchangeRealization r = FA.createComponentExchangeRealization();
        r.setSourceElement(lower);
        r.setTargetElement(upper);
        lower.getOwnedComponentExchangeRealizations().add(r);
    }

    static void realizeComp(Component lower, Component upper) {
        ComponentRealization r = CS.createComponentRealization();
        r.setSourceElement(lower);
        r.setTargetElement(upper);
        lower.getOwnedComponentRealizations().add(r);
    }

    static PhysicalLink plink(List<PhysicalLink> owner, Component a, Component b, String name, String desc,
            ComponentExchange... allocated) {
        PhysicalPort pa = CS.createPhysicalPort();
        pa.setName(name);
        a.getOwnedFeatures().add(pa);
        PhysicalPort pb = CS.createPhysicalPort();
        pb.setName(name);
        b.getOwnedFeatures().add(pb);
        PhysicalLink l = CS.createPhysicalLink();
        n(l, name, desc);
        l.getLinkEnds().add(pa);
        l.getLinkEnds().add(pb);
        owner.add(l);
        for (ComponentExchange x : allocated) {
            ComponentExchangeAllocation al = FA.createComponentExchangeAllocation();
            al.setSourceElement(l);
            al.setTargetElement(x);
            l.getOwnedComponentExchangeAllocations().add(al);
        }
        return l;
    }

    /** Functional chain through the given functions, linked by the given exchanges (fns.length == fes.length + 1). */
    static FunctionalChain chain(AbstractFunction owner, String name, String desc, List<AbstractFunction> fns,
            List<FunctionalExchange> fes) {
        FunctionalChain fc = FA.createFunctionalChain();
        n(fc, name, desc);
        owner.getOwnedFunctionalChains().add(fc);
        FunctionalChainInvolvementFunction prev = null;
        for (int i = 0; i < fns.size(); i++) {
            FunctionalChainInvolvementFunction inv = FA.createFunctionalChainInvolvementFunction();
            inv.setInvolved(fns.get(i));
            fc.getOwnedFunctionalChainInvolvements().add(inv);
            if (prev != null) {
                FunctionalChainInvolvementLink l = FA.createFunctionalChainInvolvementLink();
                l.setInvolved(fes.get(i - 1));
                l.setSource(prev);
                l.setTarget(inv);
                fc.getOwnedFunctionalChainInvolvements().add(l);
            }
            prev = inv;
        }
        return fc;
    }
}
