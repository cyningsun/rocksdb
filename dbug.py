#!/usr/bin/env python
# -*- coding: utf-8 -*-
# Usage: python dbug.py <directory> <debug_header> <debug_code>
# Example: python dbug.py src "#include\"dbug.h\"" "DBUG_TRACE;"
import os
import re
import argparse
from clang import cindex
import json
import shlex

#cindex.Config.set_library_file("/usr/local/opt/llvm/lib/libclang.dylib")
cindex.Config.set_library_file("/usr/lib/llvm-12/lib/libclang.so.1")

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
        print(f'[inline] break_start_line: {break_start_line}, break_end_line: {break_end_line}')
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
        print(f'[newline] break_start_line: {break_start_line}, break_end_line: {break_end_line}')
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

    # process children first because there are more rearward
    # Get children sorted by their position, reversed
    children = list(node.get_children())
    children.sort(key=lambda x: x.extent.start.line, reverse=True)
    for child in children:
        subChanged = False
        file_content,subChanged = process_node(source_file, child, file_content, debug_code)
        if subChanged:
            changed = True

    """递归解析AST节点，只打印来自源文件的节点"""
    if node.location.file:
        # 调试输出：打印节点位置和文件名
        #print(f"Node Location: {node.location.file.name}, Source File: {source_file}")
        
        # 检查节点是否属于源文件
        if os.path.samefile(node.location.file.name, source_file):
            if node.kind in {cindex.CursorKind.FUNCTION_DECL, cindex.CursorKind.CXX_METHOD} and not is_constexpr_function(node):
                print(f'Found function: {node.spelling} at line {node.extent.start.line}')
                subChanged = False
                file_content,subChanged = insert_line_after_function(file_content, node, debug_code)
                if subChanged:
                    changed = True

    return file_content, changed

def insert_include_after_includes(file_content, include_line):
    lines = file_content.splitlines()
    pattern = re.compile(r'^\s*(#include|#ifndef|#ifdef|#if)')
    indices = [i for i, line in enumerate(lines) if pattern.match(line) ]
    if indices:
        first_index = indices[0]
        lines.insert(first_index, include_line)
    else:
        lines.insert(0, include_line)
    return "\n".join(lines)

def insert_debug_code(node, file_path,debug_header, debug_code):
    # Read the source file
    with open(file_path, 'r') as f:
        file_content = f.read()

    # Process the AST and insert the test line in each function
    changed = False
    new_file_content,changed = process_node(file_path, node, file_content, debug_code)

    if changed:
        new_file_content = insert_include_after_includes(new_file_content, debug_header)

    #print(new_file_content)
    # write the new file content back to the file
    with open(file_path, 'w', encoding='utf-8') as file:
        file.writelines(new_file_content)

def process_files(compile_commands, directory, debug_header, debug_code):
    if not os.path.isdir(directory):
        print(f"Error: {directory} is not a valid directory.")
        return

    # 加载编译数据库
    compile_commands = load_compile_commands(compile_commands)
    
    # 初始化 Clang 的索引
    index = cindex.Index.create()
    
    skip_patterns = ['.*/dbug.h', '.*/dbug.cc', '.*/.*test.cc', '.*/.*test\w.cc']
    file_count = 0

    file_cache = {}
    for entry in compile_commands:
        file_path, args = process_compile_command(entry)
        
        if file_path is not None and args is not None:
            try:
                match = False
                for skip_pattern in skip_patterns:
                    if re.match(skip_pattern, file_path):
                        match = True
                        break
                if match:
                    print(f'Skipping file: {file_path}')
                    continue

                if not is_file_in_directory(file_path, directory):
                    print(f'not included in directory: {file_path}')
                    continue

                print(f'Processing file: {file_path}')
                if file_path in file_cache:
                    continue
                file_cache[file_path] = 1

                tu = index.parse(file_path, args)
                insert_debug_code(tu.cursor, file_path, debug_header, debug_code)
                file_count += 1
            except cindex.TranslationUnitLoadError as e:
                print(f"Failed to parse {file_path}: {e}")

    if file_count == 0:
        print(f"No matching files found in directory: {directory}")
    else:
        print(f"Processed {file_count} files in directory: {directory}")

def split_command(command):
    # 将 command 字段解析为参数列表
    arguments = shlex.split(command)

    # 从 arguments 中移除编译器和源文件路径，因为 libclang parse 不需要这些
    compiler = arguments.pop(0)
    while arguments and not arguments[0].startswith('-'):
        arguments.pop(0)

    # 处理参数，移除 -c 和 -o <output file>
    cleaned_args = []
    skip_next = False
    for arg in arguments:
        if skip_next:
            skip_next = False
            continue
        if arg in ['-c', '-o', '-march=native', '-Werror', '-fno-builtin-memcmp', '-save-temps']:
            if arg in ['-c', '-o']:
                skip_next = True  # Skip the next argument (the output file)
            continue
        else:
            cleaned_args.append(arg)

    return cleaned_args

def is_file_in_directory(file_path, directory):
    # 获取文件的绝对路径
    file_path = os.path.abspath(file_path)
    # 获取目录的绝对路径
    directory = os.path.abspath(directory) + os.path.sep

    # 判断文件路径是否以目录路径开头
    return file_path.startswith(directory)

def process_compile_command(entry):
    if 'file' in entry and 'command' in entry:
        file_path = entry['file']
        args = split_command(entry['command'])
        # 添加 -v 选项以获得更多的调试信息
        #args.append('-v')
        return file_path, args
    else:
        print("Invalid entry:", entry)
        return None, None

def load_compile_commands(path):
    with open(path, 'r') as f:
        return json.load(f)

def main():
    parser = argparse.ArgumentParser(description='Insert debug code into C++ source files using Clang AST.')
    parser.add_argument('compile_commands', type=str, help='The directory containing compile_commands.json file.')
    parser.add_argument('directory', type=str, help='The directory containing the C++ source files.')
    parser.add_argument('debug_header', type=str, help='The debug header to include in each file.')
    parser.add_argument('debug_code', type=str, help='The debug code to insert into each function.')

    args = parser.parse_args()

    process_files(args.compile_commands, args.directory, args.debug_header, args.debug_code)

if __name__ == '__main__':
    main()
