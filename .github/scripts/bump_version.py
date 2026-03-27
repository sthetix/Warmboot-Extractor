import re

with open('Versions.inc', 'r') as f:
    content = f.read()

new_content = re.sub(
    r'(LPVERSION_BUGFX = )(\d+)',
    lambda m: m.group(1) + str(int(m.group(2)) + 1),
    content
)

with open('Versions.inc', 'w') as f:
    f.write(new_content)

major = re.search(r'LPVERSION_MAJOR = (\d+)', new_content).group(1)
minor = re.search(r'LPVERSION_MINOR = (\d+)', new_content).group(1)
bugfx = re.search(r'LPVERSION_BUGFX = (\d+)', new_content).group(1)
print(f"Version bumped to {major}.{minor}.{bugfx}")
