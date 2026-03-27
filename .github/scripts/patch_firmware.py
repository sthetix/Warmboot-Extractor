import re, os, sys

date_string = os.environ['DATE_STRING']
new_hex     = os.environ['NEW_HEX']
fw_version  = os.environ['FW_VERSION']

with open('source/warmboot/warmboot_extractor.c', 'r') as f:
    content = f.read()

new_line = f'            if (memcmp(package1 + 0x10, "{date_string}", 8) == 0) return {new_hex}; // {fw_version}'

# Match every existing memcmp line in the firmware detection block
pattern = r'(            if \(memcmp\(package1 \+ 0x10, "[0-9]+", 8\) == 0\) return 0x[0-9a-fA-F]+; // [^\n]+\n)'
matches = list(re.finditer(pattern, content))

if not matches:
    print("ERROR: Could not find insertion point in warmboot_extractor.c")
    sys.exit(1)

# Insert after the last known entry
last_match = matches[-1]
content = content[:last_match.end()] + new_line + '\n' + content[last_match.end():]

with open('source/warmboot/warmboot_extractor.c', 'w') as f:
    f.write(content)

print(f"Inserted: {new_line}")
