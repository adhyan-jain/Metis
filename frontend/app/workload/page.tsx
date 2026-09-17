import { WorkloadCharacteristics } from "../../components/WorkloadCharacteristics";

export default function WorkloadPage() {
  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-xl font-bold text-slate-900">Workload Analysis</h1>
        <p className="text-sm text-slate-500 mt-1">
          Real-world corpus characteristics — identifier length, prefix similarity, scope depth, and access patterns.
        </p>
      </div>
      <WorkloadCharacteristics />
    </div>
  );
}
