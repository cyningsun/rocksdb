#!/usr/bin/env python
# -*- coding: utf-8 -*-
# Usage: python dbug.py <directory> <debug_header> <debug_code>
# Example: python dbug.py src "#include\"dbug.h\"" "DBUG_TRACE;"
import os
import re
import argparse
from clang import cindex
import glob

cindex.Config.set_library_file("/usr/local/opt/llvm/lib/libclang.dylib")

def get_function_body_lines(node):
    start_line = None
    end_line = None
    for child in node.get_children():
        if child.kind == cindex.CursorKind.COMPOUND_STMT:
            start_line = child.extent.start.line
            end_line = child.extent.end.line
            break
    return start_line, end_line

def is_constexpr_function(node):
    for token in node.get_tokens():
        if token.spelling == 'constexpr':
            return True
    return False


# Helper function to insert line after function definition
def insert_line_after_function(file_content, node, insert_line):
    lines = file_content.splitlines()
    changed = False

    break_start_line, break_end_line = get_function_body_lines(node)
    if break_start_line is None or break_end_line is None:
        return file_content, changed
    #print(f'Function body start: {body_start}, end: {body_end}')
    if break_start_line == break_end_line:
        print(f'【inline】break_start_line: {break_start_line}, break_end_line: {break_end_line}')
        # insert inline after { 
        for i in range(break_start_line-1, break_end_line):
            print(f'len(lines): {len(lines)}, i: {i}')
            if '{' in lines[i]:
                if insert_line in lines[i]:
                    break
                parts = re.split(r'(\{)', lines[i], 1)
                if len(parts) > 1:
                    indent = re.match(r'\s*', parts[2]).group()
                    parts.insert(2, indent + insert_line)
                    lines[i] = ''.join(parts)
                    changed = True
                break
    else:
        print(f'【newline】break_start_line: {break_start_line}, break_end_line: {break_end_line}')
        # insert newline after {
        for i in range(break_start_line-1, break_end_line):
            if '{' in lines[i]:
                indent = re.match(r'\s*', lines[i+1]).group()
                if insert_line in lines[i+1]:
                    break
                lines.insert(i+1, indent+insert_line)
                changed = True
                break

    return "\n".join(lines), changed

# Function to process each node in the AST
def process_node(source_file, node, file_content, debug_code):
    changed = False

    # Skip nodes that are not in the source file
    if node.location.file and node.location.file.name != source_file:
        return file_content, changed

    if node.kind in {cindex.CursorKind.FUNCTION_DECL, cindex.CursorKind.CXX_METHOD} and not is_constexpr_function(node):
        print(f'Found function: {node.spelling} at line {node.extent.start.line}')
        subChanged = False
        file_content,subChanged = insert_line_after_function(file_content, node, debug_code)
        if subChanged:
            changed = True
    
    # Get children sorted by their position, reversed
    children = list(node.get_children())
    children.sort(key=lambda x: x.extent.start.line, reverse=True)

    for child in children:
        subChanged = False
        file_content,subChanged = process_node(source_file, child, file_content, debug_code)
        if subChanged:
            changed = True

    return file_content, changed

def insert_include_after_includes(file_content, include_line):
    lines = file_content.splitlines()
    include_indices = [i for i, line in enumerate(lines) if re.match(r'#include\s+[<"].*[>"]', line)]
    if include_indices:
        first_include_index = include_indices[0]
        lines.insert(first_include_index, include_line)
    else:
        lines.insert(0, include_line)
    return "\n".join(lines)

def insert_debug_code(file_path,debug_header, debug_code):
    # Parse the source file with libclang, ignoring include files
    index = cindex.Index.create()
    args = ['-Xclang', '-fno-autolink', '-nostdinc', '-nostdinc++', '-I/nonexistentpath', '-fsyntax-only', '-isystem', '/nonexistentpath', '-D__SOURCE__','-fno-delayed-template-parsing']  # Add additional arguments as needed
    translation_unit = index.parse(file_path, args=args)

    # Read the source file
    with open(file_path, 'r') as f:
        file_content = f.read()

    # Process the AST and insert the test line in each function
    changed = False
    new_file_content,changed = process_node(file_path, translation_unit.cursor, file_content, debug_code)

    if changed:
        new_file_content = insert_include_after_includes(new_file_content, debug_header)

    #print(new_file_content)
    # write the new file content back to the file
    with open(file_path, 'w', encoding='utf-8') as file:
        file.writelines(new_file_content)

def process_files(directory, debug_header, debug_code):
    if not os.path.isdir(directory):
        print(f"Error: {directory} is not a valid directory.")
        return

    file_patterns = ['**/*.hpp', '**/*.h', '**/*.cc', '**/*.cpp']
    #file_patterns = ['**/*.cc', '**/*.cpp']
    skip_patterns = ['.*/dbug.h', '.*/dbug.cc', '.*/.*test.cc', '.*/.*test\w.cc']
    file_count = 0
    for pattern in file_patterns:
        for file_path in glob.iglob(os.path.join(directory, pattern), recursive=True):
            match = False
            for skip_pattern in skip_patterns:
                if re.match(skip_pattern, file_path):
                    match = True
                    break
            if match:
                print(f'Skipping file: {file_path}')
                continue
            print(f'Processing file: {file_path}')
            insert_debug_code(file_path,debug_header, debug_code)
            file_count += 1

    if file_count == 0:
        print(f"No matching files found in directory: {directory}")
    else:
        print(f"Processed {file_count} files in directory: {directory}")

def main():
    parser = argparse.ArgumentParser(description='Insert debug code into C++ source files using Clang AST.')
    parser.add_argument('directory', type=str, help='The directory containing the C++ source files.')
    parser.add_argument('debug_header', type=str, help='The debug header to include in each file.')
    parser.add_argument('debug_code', type=str, help='The debug code to insert into each function.')

    args = parser.parse_args()

    process_files(args.directory, args.debug_header, args.debug_code)

if __name__ == '__main__':
    main()
