import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import re
import os

sizes = [64, 128, 256, 512, 1024]
conditions = ['base', 'dnj']
files = [f"{cond}_{size}" for cond in conditions for size in sizes]

records = []

for file in files:
    if not os.path.exists(file):
        print(f"Warning: {file} not found.")
        continue
        
    try:
        with open(file, 'r') as f:
            content = f.read()
        
        condition, size = file.rsplit('_', 1)
        size = int(size)
        
        # Find the table part after "------"
        if '------' in content:
            table_str = content.split('------')[-1]
        elif '-----' in content: # fall back to fewer dashes
            table_str = content.split('-----')[-1]
        else:
            print(f"Could not find table separator in {file}")
            continue
            
        tokens = table_str.split()
        
        # Filter out 'enabled:' and '0' at the end
        tokens = [t for t in tokens if t not in ('enabled:', '0')]
        
        # Group into chunks of 4 (Stage, count, avg_ns, max_ns)
        for i in range(0, len(tokens)-3, 4):
            stage = tokens[i]
            count = int(tokens[i+1])
            avg_ns = int(tokens[i+2])
            max_ns = tokens[i+3]
            records.append({
                'Condition': condition,
                'Size': size,
                'Stage': stage,
                'Avg_ns': avg_ns
            })
    except Exception as e:
        print(f"Error parsing {file}: {e}")

df = pd.DataFrame(records)

# Check extracted data
if not df.empty:
    print("Extracted Data Head:")
    print(df.head())
    print("\nUnique Stages:")
    print(df['Stage'].unique())

    # Plotting
    stages = df['Stage'].unique()
    for stage in stages:
        df_stage = df[df['Stage'] == stage]
        
        # Pivot to have Sizes as rows, and Conditions as columns
        pivot = df_stage.pivot(index='Size', columns='Condition', values='Avg_ns')
        
        # 3. Ensure columns are in the requested order, adding rsyd
        cols = []
        if 'base' in pivot.columns: cols.append('base')
        if 'dnj' in pivot.columns: cols.append('dnj')
        pivot = pivot[cols]
        
        # 4. Added a third color (green) for the rsyd bars
        fig, ax = plt.subplots(figsize=(8, 6))
        pivot.plot(kind='bar', ax=ax, width=0.6, color=['#1f77b4', '#ff7f0e', '#2ca02c'][:len(cols)])
        
        ax.set_title(f'Average Latency: {stage}', fontsize=14)
        ax.set_xlabel('Packet Size (Bytes)', fontsize=12)
        ax.set_ylabel('Time (ns)', fontsize=12)
        plt.xticks(rotation=0)
        ax.legend(title='Condition')
        ax.grid(axis='y', linestyle='--', alpha=0.7)
        
        plt.tight_layout()
        plt.savefig(f'{stage}_latency.png')
        plt.close()

    print("Plots generated successfully.")
else:
    print("No data was extracted. Please check the file paths and contents.")