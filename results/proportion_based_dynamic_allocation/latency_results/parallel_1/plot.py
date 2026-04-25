import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import re
import os

files = [
    'dny_policy_64', 'dny_policy_128', 'dny_policy_256', 'dny_policy_512', 'dny_policy_1024',
    'base_64', 'base_128', 'base_256', 'base_512', 'base_1024'
]

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
        
        if '------' in content:
            table_str = content.split('------')[-1]
        elif '-----' in content: # fall back to fewer dashes
            table_str = content.split('-----')[-1]
        else:
            print(f"Could not find table separator in {file}")
            continue
            
        tokens = table_str.split()
        
        tokens = [t for t in tokens if t not in ('enabled:', '0')]
        
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

print("Extracted Data Head:")
print(df.head())
print("\nUnique Stages:")
print(df['Stage'].unique())

stages = df['Stage'].unique()
for stage in stages:
    df_stage = df[df['Stage'] == stage]
    
    pivot = df_stage.pivot(index='Size', columns='Condition', values='Avg_ns')
    
    cols = []
    if 'base' in pivot.columns: cols.append('base')
    if 'dny_policy' in pivot.columns: cols.append('dny_policy')
    pivot = pivot[cols]
    
    fig, ax = plt.subplots(figsize=(8, 6))
    pivot.plot(kind='bar', ax=ax, width=0.6, color=['#1f77b4', '#ff7f0e'])
    
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
