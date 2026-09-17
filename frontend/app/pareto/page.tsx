import { ParetoView } from "../../components/ParetoView";

export default function ParetoPage() {
  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-xl font-bold text-slate-900">Pareto Frontier</h1>
        <p className="text-sm text-slate-500 mt-1">
          Memory vs. latency tradeoff across configurations — the most compressed configuration is not automatically the best one.
        </p>
      </div>
      <ParetoView />
    </div>
  );
}
