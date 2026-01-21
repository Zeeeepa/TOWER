import React, { useState } from 'react';
import { Service, ServiceFlow } from '../types';
import { ArrowLeft, Play, Save, Trash2, Crosshair, MousePointer, Type, Check } from 'lucide-react';
import { generateSimulationResponse } from '../services/geminiService';

interface FlowConfigProps {
  service: Service;
  onBack: () => void;
  onUpdateFlows: (serviceId: string, flows: ServiceFlow[]) => void;
}

const FlowConfig: React.FC<FlowConfigProps> = ({ service, onBack, onUpdateFlows }) => {
  const [testOutput, setTestOutput] = useState<string>("");
  const [isTesting, setIsTesting] = useState(false);
  const [activeTab, setActiveTab] = useState<'flows' | 'tester'>('flows');
  const [chatInput, setChatInput] = useState('');

  const handleTestFlow = (flow: ServiceFlow) => {
    setIsTesting(true);
    setTestOutput(`[SYSTEM] Initiating flow: ${flow.name}...\n[AGENT] Verifying selectors...\n[AGENT] Executing steps: ${flow.steps.join(' -> ')}...`);
    
    setTimeout(() => {
        setTestOutput(prev => prev + `\n[SUCCESS] Flow executed successfully. State verified.`);
        setIsTesting(false);
    }, 1500);
  };

  const handleSendChat = async () => {
    if(!chatInput.trim()) return;
    const msg = chatInput;
    setChatInput('');
    setTestOutput(prev => prev + `\n\n> USER: ${msg}`);
    setIsTesting(true);

    const response = await generateSimulationResponse(
        `Simulate a response from the AI service '${service.name}' for the prompt: "${msg}". Keep it short.`, 
        `You are simulating the backend of ${service.name}.`
    );

    setTestOutput(prev => prev + `\n> ${service.name.toUpperCase()}: ${response}`);
    setIsTesting(false);
  };

  return (
    <div className="flex flex-col h-full bg-tower-900">
      {/* Header */}
      <div className="p-4 border-b border-tower-600 flex justify-between items-center bg-tower-800">
        <div className="flex items-center gap-4">
          <button onClick={onBack} className="p-2 hover:bg-tower-700 rounded-full text-gray-400 hover:text-white">
            <ArrowLeft size={20} />
          </button>
          <div>
            <h2 className="text-xl font-bold text-white">{service.name} <span className="text-gray-500 text-sm font-normal">({service.url})</span></h2>
            <div className="flex items-center gap-2 text-xs font-mono text-gray-400">
                <span className={service.status === 'online' ? 'text-tower-success' : 'text-tower-danger'}>● {service.status.toUpperCase()}</span>
                <span>|</span>
                <span>AUTH: {service.authType.toUpperCase()}</span>
            </div>
          </div>
        </div>
        <div className="flex gap-2">
            <button 
                onClick={() => setActiveTab('flows')}
                className={`px-4 py-2 rounded text-sm font-bold ${activeTab === 'flows' ? 'bg-tower-accent text-black' : 'text-gray-400 hover:text-white'}`}
            >
                FLOWS
            </button>
            <button 
                onClick={() => setActiveTab('tester')}
                className={`px-4 py-2 rounded text-sm font-bold ${activeTab === 'tester' ? 'bg-tower-accent text-black' : 'text-gray-400 hover:text-white'}`}
            >
                TEST INTERFACE
            </button>
        </div>
      </div>

      <div className="flex-1 overflow-hidden flex">
        {activeTab === 'flows' ? (
             <div className="flex-1 p-6 overflow-y-auto">
             <div className="flex justify-between items-center mb-4">
               <h3 className="text-lg font-bold text-white flex items-center gap-2"><Crosshair size={18} /> DISCOVERED FLOWS</h3>
               <span className="text-xs font-mono text-purple-400">VISION ANALYSIS COMPLETE</span>
             </div>
             
             <div className="space-y-4">
               {service.flows.map((flow) => (
                 <div key={flow.id} className="bg-tower-800 border border-tower-600 p-4 rounded-lg hover:border-tower-accent transition-colors">
                   <div className="flex justify-between items-start">
                     <div>
                       <div className="flex items-center gap-2">
                         <h4 className="font-bold text-white">{flow.name}</h4>
                         <span className="px-2 py-0.5 bg-green-900/50 text-green-400 text-[10px] rounded border border-green-800 uppercase">{flow.status}</span>
                       </div>
                       <p className="text-sm text-gray-400 mt-1">{flow.description}</p>
                     </div>
                     <button 
                        onClick={() => handleTestFlow(flow)}
                        className="bg-tower-700 hover:bg-tower-600 text-white p-2 rounded transition-colors" title="Test Flow"
                     >
                       <Play size={16} />
                     </button>
                   </div>
                   
                   <div className="mt-4 bg-black/30 p-3 rounded font-mono text-xs text-gray-400 border border-tower-700">
                     <p className="mb-2 text-tower-accent/70 font-bold border-b border-tower-700 pb-1">PROGRAMMATIC STEPS:</p>
                     <ul className="space-y-1">
                        {flow.steps.map((step, idx) => (
                            <li key={idx} className="flex items-center gap-2">
                                <span className="text-gray-600">{idx + 1}.</span>
                                {step.includes('Click') && <MousePointer size={10} className="text-blue-400"/>}
                                {step.includes('Input') && <Type size={10} className="text-yellow-400"/>}
                                {step.includes('Verify') && <Check size={10} className="text-green-400"/>}
                                <span>{step}</span>
                            </li>
                        ))}
                     </ul>
                   </div>
                 </div>
               ))}
             </div>
           </div>
        ) : (
            <div className="flex-1 flex flex-col p-6 h-full">
                <div className="flex-1 bg-black/40 border border-tower-600 rounded-lg p-4 font-mono text-sm overflow-y-auto mb-4 whitespace-pre-wrap text-gray-300 shadow-inner">
                    {testOutput || <span className="text-gray-600 italic">Chat session initialized. Connected to {service.name} API Proxy...</span>}
                </div>
                <div className="flex gap-2">
                    <input 
                        type="text" 
                        className="flex-1 bg-tower-800 border border-tower-600 rounded p-3 text-white focus:border-tower-accent focus:outline-none"
                        placeholder="Type a message to test the API..."
                        value={chatInput}
                        onChange={(e) => setChatInput(e.target.value)}
                        onKeyDown={(e) => e.key === 'Enter' && handleSendChat()}
                        disabled={isTesting}
                    />
                    <button 
                        onClick={handleSendChat}
                        disabled={isTesting}
                        className="bg-tower-accent text-black font-bold px-6 rounded hover:bg-cyan-400 disabled:opacity-50"
                    >
                        SEND
                    </button>
                </div>
            </div>
        )}
      </div>
    </div>
  );
};

export default FlowConfig;