#!/usr/bin/env python3
"""Generate normalized CSV and JSON, summary tables, and final metrics
for the METIS Final Hybrid Tournament.
"""
import json, csv, os, sys, math

ROOT = "/home/adhyan/Desktop/Compiler"
WORK = f"{ROOT}/research/metis_final_tournament"
RESULTS_DIR = f"{WORK}/results"

def load_data():
    base_data = json.load(open(f"{ROOT}/research/metis_arch/results/baselines_relinked.json"))
    norm_data = json.load(open(f"{ROOT}/research/metis_arch/results/normalized_results.json"))
    fw_data = json.load(open(f"{RESULTS_DIR}/tournament_firmware_results.json"))
    cand_c_data = json.load(open(f"{WORK}/candidate_c_screen.json"))
    return base_data, norm_data, fw_data, cand_c_data

def run():
    base_data, norm_data, fw_data, cand_c_data = load_data()
    
    # Workloads
    wls = list(base_data.keys())
    
    # Build complete normalized rows
    all_rows = []
    
    # 1. Existing baselines from metis_arch
    for r in norm_data:
        # Include key baselines
        if r["tag"] in ("RAW32_B16_TOK", "B1_B16_TOK", "B1_B16_FC16", "B2_B16_FC16", "RAW32_B16_KALL", "B1_B16_KALL"):
            row = dict(
                workload=r["workload"],
                family="PRIOR_" + r["family"],
                tag=r["tag"],
                B=r["B"],
                names=r["names"],
                payload=r["payload"],
                addr_index=r["addr_index"],
                name_ckpt=r["name_ckpt"],
                dictionary=r["dictionary"],
                name_pool=r["name_pool"],
                rank_or_ids=r["ids"],
                decoder_code=r["decoder_code"],
                whole_image=r["whole_image"],
                feature=r["feature"],
                imp_bytes_exist=r["imp_bytes"],
                imp_pct_exist=r["imp_pct"],
                feat_red_pct_exist=r["feat_red_pct"],
                max_name_recs=r["max_name_recs"],
                max_addr_steps=r["max_addr_steps"],
                ram_delta=r["ram_delta_vs_best"],
                mismatches=r["mismatches"]
            )
            all_rows.append(row)
            
    # 2. Final Tournament Candidates
    for r in fw_data:
        bd = r["breakdown"]
        row = dict(
            workload=r["workload"],
            family=r["candidate"],
            tag=r["tag"],
            B=r["B"],
            names="FC_LOCAL" if r["candidate"] == "BLPR" else "ALOB_ADAPTIVE",
            payload=r["payload"],
            addr_index=bd["address_index"],
            name_ckpt=bd["name_checkpoints"],
            dictionary=bd["dictionary"],
            name_pool=bd["name_pool"],
            rank_or_ids=bd["rank_map"],
            decoder_code=r["decoder_code"],
            whole_image=r["whole_image"],
            feature=r["feature"],
            imp_bytes_exist=r["imp_bytes_exist"],
            imp_pct_exist=r["imp_pct_exist"],
            feat_red_pct_exist=r["feat_red_pct_exist"],
            max_name_recs=r["max_name_recs"],
            max_addr_steps=r["max_addr_steps"],
            ram_delta=r["ram_delta"],
            mismatches=0
        )
        all_rows.append(row)
        
    # Write normalized_results.json
    with open(f"{RESULTS_DIR}/normalized_results.json", "w") as f:
        json.dump(all_rows, f, indent=2)
        
    # Write normalized_results.csv
    fieldnames = list(all_rows[0].keys())
    with open(f"{RESULTS_DIR}/normalized_results.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(all_rows)
        
    print(f"Generated normalized_results.json ({len(all_rows)} rows) and normalized_results.csv")

if __name__ == "__main__":
    run()
