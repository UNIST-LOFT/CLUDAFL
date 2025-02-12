#!/usr/bin/env python3.8

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

good_path=f'{argv.cludafl_path}/cludafl/good'
bad_path=f'{argv.cludafl_path}/cludafl/bad'
new_input_dir=f'{argv.cludafl_path}/cludafl/queue'
tmp_input_name = f'{argv.cludafl_path}/cludafl/.tmp_input' # CLUDAFL ignores files starting with '.'

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
        print(f'{event.src_path} is created.',file=sys.stderr)
        if InputDetector.dafl_generating:
            # Other detector is already handling
            return
        
        InputDetector.dafl_generating=True
        sleep(1) # Wait until every files are created by CLUDAFL
        
        # Read the given inputs from files
        good_inputs:List[str]=[]
        bad_inputs:List[str]=[]
        good_cnt = 0
        for file in os.listdir(good_path):
            good_cnt += 1
            if good_cnt > 2:
                break
            with open(f'{good_path}/{file}','r') as f:
                good_inputs.append(f.read())
            os.remove(f'{good_path}/{file}')
        bad_cnt = 0
        for file in os.listdir(bad_path):
            bad_cnt += 1
            if bad_cnt > 2:
                break
            with open(f'{bad_path}/{file}','r') as f:
                bad_inputs.append(f.read())
            os.remove(f'{bad_path}/{file}')
        
        # Replace special characters for JSON
        for i,input in enumerate(good_inputs.copy()):
            input=input.replace('\n','\\n')
            input=input.replace('"','\\"')
            input=input.replace('\t','    ')
            good_inputs[i]=input
        for i,input in enumerate(bad_inputs.copy()):
            input=input.replace('\n','\\n')
            input=input.replace('"','\\"')
            input=input.replace('\t','    ')
            bad_inputs[i]=input

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
            
        data='{'+ \
            '"model":"gpt-4o",'+ \
            '"messages":'+ \
            '['+ \
                '{'+ \
                    '"role":"developer",'+ \
                    '"content":"You are the best software engineer.\\n'+ \
                        'You will take some text inputs for a C program.\\n'+ \
                        'Generate an input seed for my fuzzer that generates an input that has new program state when program crashed.\\n"'+ \
                '},'+ \
                '{'+ \
                    '"role":"user",'+ \
                    f'"content":"{user_msg}"'+ \
                '}'+ \
            ']'+ \
        '}'
        print(data,file=sys.stderr)
        json.loads(data) # Check if data is valid JSON

        req=requests.post('https://api.openai.com/v1/chat/completions',headers={
            'Content-Type':'application/json',
            'Authorization':f'Bearer {os.getenv("OPENAI_API_KEY")}'
        },data=data)

        # Generate new input file in temporary directory and make a symbolic link
        with open(tmp_input_name,'w') as f:
            f.write(req.json()['choices'][0]['message']['content'].replace('```\n','').replace('\n```',''))
        if os.path.exists(f'{new_input_dir}/.tmp_input'):
            os.remove(f'{new_input_dir}/.tmp_input')
        os.symlink(tmp_input_name, f'{new_input_dir}/.tmp_input')
        print('New input is generated.',file=sys.stderr)

        InputDetector.dafl_generating=False

if __name__=='__main__':
    def stop_signal(signum,frame):
        observer.stop()
        print('GPT generator terminated.',file=sys.stderr)
        exit(0)

    signal.signal(signal.SIGINT,stop_signal)
    observer=observers.Observer()
    observer.schedule(InputDetector(),good_path)
    observer.schedule(InputDetector(),bad_path)
    observer.start()
    observer.join()