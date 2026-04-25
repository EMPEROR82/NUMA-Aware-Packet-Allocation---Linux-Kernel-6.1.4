import pandas as pd
import matplotlib.pyplot as plt
import os

files = [
    'dny_policy_64', 'dny_policy_128', 'dny_policy_256', 'dny_policy_512', 'dny_policy_1024',
    'base_64', 'base_128', 'base_256', 'base_512', 'base_1024'
]

records = []

for file in files:
    if not os.path.exists(file):
        continue
    
    condition, size = file.rsplit('_', 1)
    size = int(size)
    
    with open(file, 'r') as f:
        lines = f.readlines()
        
    for line in lines:
        if 'AGGREGATE' in line:
            parts = line.strip().split(',')
            throughput_str = parts[3]
            avg_throughput = float(throughput_str.split(';')[0].split('=')[1])
            
            records.append({
                'Condition': condition,
                'Size': size,
                'Throughput_Mbps': avg_throughput
            })

df = pd.DataFrame(records)
print(df)

pivot = df.pivot(index='Size', columns='Condition', values='Throughput_Mbps')
    
fig, ax = plt.subplots(figsize=(8, 6))
pivot.plot(kind='line', marker='o', ax=ax, color=['#1f77b4', '#ff7f0e'])

ax.set_title('Throughput vs Packet Size', fontsize=14)
ax.set_xlabel('Packet Size (Bytes)', fontsize=12)
ax.set_ylabel('Throughput (Mbps)', fontsize=12)
plt.xticks(pivot.index)
ax.legend(title='Condition')
ax.grid(True, linestyle='--', alpha=0.7)

plt.tight_layout()
plt.savefig('throughput_line_plot.png')
plt.close()

print("Plot generated successfully.")
