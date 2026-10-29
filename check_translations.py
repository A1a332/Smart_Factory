#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Check the translation files in Content/Localization against the source code.

A translation is only shown in game when its "Source" matches the English text in the
code exactly. This script finds every WT_LOCTEXT / NSLOCTEXT("Remote_Factory", ...) in
Source/ and reports:
  - keys used with two different English texts
  - keys missing from a translation file, or with an empty translation
  - keys whose English in the file no longer matches the code
  - keys in a file that the code no longer uses

RUN
---
  python check_translations.py          check only, exit code 1 on problems
  python check_translations.py --sync   update every file to match the code:
                                        add new keys (empty translation), update changed
                                        English (translation kept, listed for review),
                                        drop unused keys
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent
SOURCE_DIRECTORY = ROOT / 'Source' / 'Remote_Factory'
TRANSLATION_DIRECTORY = ROOT / 'Content' / 'Localization'
NAMESPACE = 'Remote_Factory'

ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', "'": "'", '\\': '\\', '0': '\0'}


def strip_comments(text):
    """Blank out comments but keep string literals and line numbers."""
    output = []
    index, length = 0, len(text)
    while index < length:
        if text.startswith('//', index):
            end = text.find('\n', index)
            index = length if end < 0 else end
        elif text.startswith('/*', index):
            end = text.find('*/', index + 2)
            end = length if end < 0 else end + 2
            output.append('\n' * text.count('\n', index, end))
            index = end
        elif text[index] in '"\'':
            quote = text[index]
            end = index + 1
            while end < length and text[end] != quote:
                end += 2 if text[end] == '\\' else 1
            output.append(text[index:end + 1])
            index = end + 1
        else:
            output.append(text[index])
            index += 1
    return ''.join(output)


def read_literals(text, index):
    """Read one or more adjacent string literals starting at index. Returns (value, next index)."""
    parts = []
    literal = re.compile(r'\s*(?:TEXT\(\s*)?"((?:[^"\\]|\\.)*)"\s*\)?')
    while True:
        match = literal.match(text, index)
        if not match:
            break
        parts.append(re.sub(r'\\(.)', lambda escape: ESCAPES.get(escape.group(1), escape.group(1)), match.group(1)))
        index = match.end()
    return (''.join(parts) if parts else None), index


def collect_source_strings():
    """Returns {key: {english: [locations]}}."""
    found = {}
    call = re.compile(r'\b(WT_LOCTEXT|NSLOCTEXT)\s*\(')
    for path in sorted(SOURCE_DIRECTORY.glob('*.[ch]*')):
        if path.suffix not in ('.cpp', '.h'):
            continue
        text = strip_comments(path.read_text(encoding='utf-8-sig'))
        for match in call.finditer(text):
            index = match.end()
            if match.group(1) == 'NSLOCTEXT':
                namespace, index = read_literals(text, index)
                if namespace != NAMESPACE or not text.startswith(',', index):
                    continue
                index += 1
            key, index = read_literals(text, index)
            if key is None or not text.startswith(',', index):
                continue
            english, index = read_literals(text, index + 1)
            if english is None:
                continue
            line = text.count('\n', 0, match.start()) + 1
            found.setdefault(key, {}).setdefault(english, []).append(f'{path.name}:{line}')
    return found


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, data):
    text = json.dumps(data, ensure_ascii=False, indent='\t') + '\n'
    path.write_bytes(text.encode('utf-8'))


def main():
    sync = '--sync' in sys.argv[1:]
    source_strings = collect_source_strings()
    problems = 0

    english_by_key = {}
    for key, variants in sorted(source_strings.items()):
        if len(variants) > 1:
            problems += 1
            print(f'[conflict] {key} has {len(variants)} different English texts:')
            for english, locations in variants.items():
                print(f'    {english!r}  ({", ".join(locations)})')
        english_by_key[key] = next(iter(variants))

    for path in sorted(TRANSLATION_DIRECTORY.glob('*.json')):
        data = load(path)
        entries = data.get('Entries', {})
        name = path.name

        missing = [key for key in english_by_key if key not in entries]
        unused = [key for key in entries if key not in english_by_key]
        changed = [key for key in english_by_key
                   if key in entries and entries[key].get('Source') != english_by_key[key]]
        untranslated = [key for key in english_by_key
                        if key in entries and not entries[key].get('Translation')]

        for key in missing:
            print(f'[{name}] missing: {key}  ({english_by_key[key]!r})')
        for key in changed:
            print(f'[{name}] English changed: {key}\n    file: {entries[key].get("Source")!r}\n    code: {english_by_key[key]!r}')
        for key in untranslated:
            print(f'[{name}] not translated yet: {key}  ({english_by_key[key]!r})')
        for key in unused:
            print(f'[{name}] no longer used: {key}')

        if sync:
            synced = {}
            for key in sorted(english_by_key):
                translation = entries.get(key, {}).get('Translation', '')
                synced[key] = {'Source': english_by_key[key], 'Translation': translation}
            data['Entries'] = synced
            save(path, data)
            if changed:
                print(f'[{name}] synced. Check these translations still fit the new English: {", ".join(changed)}')
            untranslated = [key for key in synced if not synced[key]['Translation']]
            problems += len(untranslated)
        else:
            problems += len(missing) + len(changed) + len(untranslated) + len(unused)

    print(f'{len(english_by_key)} keys in code, {problems} problem(s).')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
