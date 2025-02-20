#!/usr/bin/env python3

import argparse
import json
import signal
from typing import List
import requests
import os
import sys
import uuid
from watchdog import observers,events
from time import sleep
# We do not use openai library for compatibility with various Python version

args=argparse.ArgumentParser('gpt.py')
args.add_argument('program',type=str,help='Program name')
args.add_argument('cludafl_path',type=str,help='Path to CLUDAFL out dir')

argv=args.parse_args()

prev_input_path=f'{argv.cludafl_path}/cludafl/input-'
new_input_dir=f'{argv.cludafl_path}/cludafl/queue'
tmp_input_name = f'{argv.cludafl_path}/cludafl/.tmp_input' # CLUDAFL ignores files starting with '.'
tmp_input_count = 0

# Watchdog watches the directory for new files
# If new files are created, it means that CLUDAFL wants to generate a new input from LLM
class InputDetector(events.FileSystemEventHandler):
    dafl_generating:bool=False
    llm_input_gen_cnt: int
    def __init__(self):
        self.dafl_generating = False
        self.llm_input_gen_cnt = 0
        pass

    def on_created(self,event:events.FileCreatedEvent):
        if not event.src_path.startswith(prev_input_path):
            return
        print(f'{event.src_path} is created.',file=sys.stderr)
        if InputDetector.dafl_generating:
            # Other detector is already handling
            return
        
        InputDetector.dafl_generating=True
        sleep(.5)
        global tmp_input_count
        tmp_input_count += 1
        
        good_input_path,bad_input_path=[],[]
        with open(event.src_path,'r') as f:
            for line in f:
                if line.startswith('good'):
                    good_input_path.append(line.split('\t')[1].strip())
                elif line.startswith('bad'):
                    bad_input_path.append(line.split('\t')[1].strip())

        # Read the given inputs from files
        good_inputs:List[str]=[]
        bad_inputs:List[str]=[]
        for file in good_input_path:
            print(f'good file: {file}',file=sys.stderr)
            try:
                with open(file,'rb') as f:
                    input=f.read()
                    input=input.decode('utf-8',errors='ignore')
                    input=input.replace('\n','\\n')
                    input=input.replace('\r','\\r')
                    input=input.replace('\x00','\\x')
                    input=input.replace('"','\\"')
                    input=input.replace('\t','    ')
                    json.loads('["'+input+'"]') # Check if input is valid JSON
                    good_inputs.append(input)
            except Exception as e:
                print(f'Error: {file} is not a file or contains invalid character: {e}',file=sys.stderr)
            os.remove(file)
        for file in bad_input_path:
            print(f'bad file: {file}',file=sys.stderr)
            try:
                with open(file,'rb') as f:
                    input=f.read()
                    input=input.decode('utf-8',errors='ignore')
                    input=input.replace('\n','\\n')
                    input=input.replace('\r','\\r')
                    input=input.replace('"','\\"')
                    input=input.replace('\t','    ')
                    json.loads('["'+input+'"]') # Check if input is valid JSON
                    bad_inputs.append(input)
            except Exception as e:
                print(f'Error: {file} is not a file or contains invalid character: {e}',file=sys.stderr)
            os.remove(file)

        user_msg=f"Below is the inputs for {argv.program} program "
        if len(good_inputs)>0 and len(bad_inputs)>0:
            user_msg+="that are helpful to generate new program states or not. Please generate a new program input that can generate unique program states.\\n\\n"+ \
                "* Inputs that contributes to generate unique program states:\\n"
            for i,input in enumerate(good_inputs):
                user_msg+=f"{i+1}.\\n```\\n{input}\\n```\\n"
            user_msg+="* Inputs that does NOT contribute to generate unique program states:\\n"
            for i,input in enumerate(bad_inputs):
                user_msg+=f"{i+1}.\\n```\\n{input}\\n```\\n"
        elif len(good_inputs)>0:
            user_msg+="that are helpful to generate new program states. Please generate a new program input that can generate unique program states.\\n\\n"+ \
                "* Inputs that contributes to generate unique program states:\\n"
            for i,input in enumerate(good_inputs):
                user_msg+=f"{i+1}.\\n```\\n{input}\\n```\\n"
        elif len(bad_inputs)>0:
            user_msg+="that are NOT helpful to generate new program states. Please generate a new program input that can generate unique program states.\\n\\n"+ \
                "* Inputs that does NOT contribute to generate unique program states:\\n"
            for i,input in enumerate(bad_inputs):
                user_msg+=f"{i+1}.\\n```\\n{input}\\n```\\n"
        
        user_msg+="Please follow the rules below:\\n"+ \
            "1. Do NOT give any description.\\n"+ \
            "2. Give me the new inputs ONLY between ``` and ```.\\n"+ \
            "3. Just generate ONE input. Do not generate multiple inputs.\\n"+ \
            "4. New program input should follow its own input format.\\n"+ \
            "5. New program input should occur crash."
        
        # Add extra rules for project-specific
        if 'xml' in argv.program: # For libxml2
            user_msg+="\\n6. New program input should be a valid XML format."
            
        data={"model":"gpt-4o",
            "messages":[{"role":"developer",
                    "content":"You are the best software engineer.\\n"+ \
                        'You will take some text inputs for a C program.\\n'+ \
                        'Generate an input seed for my fuzzer that generates an input that has new program state when program crashed.\\n'
                },
                {
                    "role":"user",
                    "content":f"{user_msg}"
                }
            ]
        }
        print(data,file=sys.stderr)

        req=requests.post('https://api.openai.com/v1/chat/completions',headers={
            'Content-Type':'application/json',
            'Authorization':f'Bearer {os.getenv("OPENAI_API_KEY")}'
        },data=json.dumps(data))
        res=req.json()
        print(f'New input is generated:\n{res}',file=sys.stderr)
        res_output=res['choices'][0]['message']['content'].replace('```\n','').replace('\n```','')
        # Generate new input file in temporary directory and make a symbolic link
        with open(f'{tmp_input_name}-{tmp_input_count}','w') as f:
            f.write(res_output)
        os.link(f'{tmp_input_name}-{tmp_input_count}', f'{new_input_dir}/tmp_input-{tmp_input_count}')

        InputDetector.dafl_generating=False

if __name__=='__main__':
    def stop_signal(signum,frame):
        observer.stop()
        print('GPT generator terminated.',file=sys.stderr)
        exit(0)

    signal.signal(signal.SIGINT,stop_signal)
    signal.signal(signal.SIGTERM,stop_signal)
    observer=observers.Observer()
    observer.schedule(InputDetector(),f'{argv.cludafl_path}/cludafl')
    observer.start()
    observer.join()