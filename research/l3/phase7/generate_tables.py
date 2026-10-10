import csv

input_file = "results/metis_x_ablation.csv"
output_file = "research/l3/phase7/table_1_results.tex"

data = []
try:
    with open(input_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            data.append(row)
except Exception as e:
    print(f"Error reading {input_file}: {e}")
    exit(1)

# Format into LaTeX table
with open(output_file, 'w') as f:
    f.write("\\begin{table}[h]\n")
    f.write("\\centering\n")
    f.write("\\begin{tabular}{llrr}\n")
    f.write("\\toprule\n")
    f.write("Corpus & Architecture & Peak Heap (MB) & $p_{95}$ Latency ($\\mu$s) \\\\\n")
    f.write("\\midrule\n")
    
    for row in data:
        # Convert bytes to MB
        heap_mb = float(row['final_heap_bytes']) / (1024 * 1024)
        latency = float(row['lookup_p95_us'])
        corpus = row['corpus']
        # Clean up ablation level names for the paper
        arch = row['ablation_level'].replace('_', '\\_')
        
        f.write(f"{corpus} & {arch} & {heap_mb:.2f} & {latency:.3f} \\\\\n")
        
    f.write("\\bottomrule\n")
    f.write("\\end{tabular}\n")
    f.write("\\caption{Authoritative Canonical Results from Phase 2 Benchmark.}\n")
    f.write("\\label{tab:canonical_results}\n")
    f.write("\\end{table}\n")

print(f"Successfully generated {output_file}")
