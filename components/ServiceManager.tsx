import React, { useState, useRef, useEffect } from 'react';
import { Service, ServiceConfig, ServiceType, ZaiMode } from '../types';
import { 
  Shield, Globe, Settings, RefreshCw, Search, Brain, 
  Presentation, Layers, Wand2, Terminal, BookOpen, 
  Paperclip, Send, ChevronDown, Activity, Zap, 
  ToggleLeft, ToggleRight, CheckCircle, AlertTriangle, Eye, Lock
} from 'lucide-react';

interface ServiceManagerProps {
  services: Service[];
  onAddService: (url: string, email: string, pass: string) => void;
  onSelectService: (service: Service) => void;
  onToggleStatus: (id: string) => void;
  onUpdateService: (id: string, updates: Partial<Service>) => void;
  onSendMessage: (id: string, payload: any) => void;
  onRunDiscovery: (id: string) => void;
}

const ServiceManager: React.FC<ServiceManagerProps> = ({ 
  services, 
  onAddService, 
  onSelectService, 
  onToggleStatus, 
  onUpdateService,
  onSendMessage,
  onRunDiscovery
}) => {
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [globalInput, setGlobalInput] = useState('');
  
  // Modal State
  const [newUrl, setNewUrl] = useState('');
  const [newEmail, setNewEmail] = useState('');
  const [newPass, setNewPass] = useState('');

  const handleAdd = (e: React.FormEvent) => {
    e.preventDefault();
    onAddService(newUrl, newEmail, newPass);
    setIsModalOpen(false);
    resetForm();
  };

  const resetForm = () => {
    setNewUrl('');
    setNewEmail('');
    setNewPass('');
  };

  const handleToggleSelection = (id: string) => {
    const service = services.find(s => s.id === id);
    if (service) {
        onUpdateService(id, { isSelected: !service.isSelected });
    }
  };

  const handleToggleExpand = (id: string) => {
    const service = services.find(s => s.id === id);
    if (service) {
        onUpdateService(id, { expanded: !service.expanded });
    }
  };

  const handleGlobalSend = () => {
    if (!globalInput.trim()) return;
    
    const selectedServices = services.filter(s => s.isSelected && s.status === 'online');
    if (selectedServices.length === 0) return;

    selectedServices.forEach(service => {
      const payload = {
        prompt: globalInput,
        configuration: {
          model: service.config.model,
          features: service.config // Send the specific config
        },
        timestamp: new Date().toISOString()
      };
      onSendMessage(service.id, payload);
    });
    setGlobalInput('');
  };

  // --- Feature Toggles Components ---

  const Toggle = ({ active, onClick, disabled = false, size = 'md' }: { active: boolean, onClick: () => void, disabled?: boolean, size?: 'sm'|'md' }) => (
    <button 
        onClick={(e) => { e.stopPropagation(); onClick(); }}
        disabled={disabled}
        className={`relative transition-colors rounded-full ${size === 'sm' ? 'w-8 h-4' : 'w-10 h-5'} ${active ? 'bg-tower-accent' : 'bg-tower-600'} ${disabled ? 'opacity-40 cursor-not-allowed' : ''}`}
    >
        <div className={`absolute top-0.5 bg-white rounded-full transition-transform ${size === 'sm' ? 'w-3 h-3' : 'w-4 h-4'} ${active ? (size === 'sm' ? 'translate-x-4' : 'translate-x-5') : 'translate-x-0.5'}`} />
    </button>
  );

  const FeatureBadge = ({ label, active, onClick, icon: Icon, disabled }: any) => (
    <div 
        onClick={!disabled ? onClick : undefined}
        className={`flex items-center gap-2 px-2 py-1 rounded cursor-pointer select-none transition-all ${
            active 
            ? 'bg-tower-700 border border-tower-accent/50 text-white' 
            : 'bg-transparent border border-transparent text-gray-500 hover:text-gray-300'
        } ${disabled ? 'opacity-30 cursor-not-allowed' : ''}`}
    >
        {Icon && <Icon size={12} className={active ? 'text-tower-accent' : ''} />}
        <span className="text-[10px] font-bold uppercase tracking-wide">{label}</span>
        <div className={`w-1.5 h-1.5 rounded-full ${active ? 'bg-tower-accent shadow-[0_0_5px_rgba(0,240,255,0.8)]' : 'bg-tower-600'}`} />
    </div>
  );

  // --- Service Specific Renderers ---

  const renderZaiControls = (service: Service) => {
    const update = (k: keyof ServiceConfig, v: any) => {
        const newConfig = { ...service.config, [k]: v };
        if (k === 'search' && !v) newConfig.advancedSearch = false; // Dependency
        if (k === 'advancedSearch' && v) newConfig.search = true;   // Dependency
        onUpdateService(service.id, { config: newConfig });
    };

    return (
        <div className="flex flex-col gap-2 w-full">
            <div className="flex flex-wrap items-center gap-3">
                <FeatureBadge label="DeepThink" active={service.config.deepThink} onClick={() => update('deepThink', !service.config.deepThink)} icon={Brain} />
                <div className="h-3 w-px bg-tower-700" />
                <FeatureBadge label="Search" active={service.config.search} onClick={() => update('search', !service.config.search)} icon={Search} />
                <FeatureBadge label="Adv. Search" active={service.config.advancedSearch} onClick={() => update('advancedSearch', !service.config.advancedSearch)} disabled={!service.config.search} icon={Search} />
            </div>
            {/* Modes */}
            <div className="flex items-center gap-1 overflow-x-auto no-scrollbar pt-1">
                 {[
                  { id: 'general', label: 'General', icon: Activity },
                  { id: 'deep-research', label: 'Research', icon: BookOpen },
                  { id: 'magic-design', label: 'Design', icon: Wand2 },
                  { id: 'full-stack', label: 'Code', icon: Layers },
                  { id: 'ai-slides', label: 'Slides', icon: Presentation },
                ].map(mode => (
                    <button
                        key={mode.id}
                        onClick={() => update('mode', mode.id)}
                        className={`px-2 py-0.5 rounded text-[9px] uppercase font-bold border transition-colors flex items-center gap-1 ${
                            service.config.mode === mode.id
                            ? 'bg-tower-accent/10 border-tower-accent text-tower-accent'
                            : 'bg-tower-800 border-tower-700 text-gray-500 hover:border-gray-500'
                        }`}
                    >
                        <mode.icon size={10} /> {mode.label}
                    </button>
                ))}
            </div>
        </div>
    );
  };

  const renderChatGPTControls = (service: Service) => {
    const update = (k: keyof ServiceConfig, v: any) => onUpdateService(service.id, { config: { ...service.config, [k]: v } });
    return (
        <div className="flex items-center gap-4 w-full">
             <FeatureBadge label="Web Browsing" active={service.config.webBrowsing} onClick={() => update('webBrowsing', !service.config.webBrowsing)} icon={Globe} />
             <FeatureBadge label="DALL·E 3" active={service.config.dalle} onClick={() => update('dalle', !service.config.dalle)} icon={Wand2} />
             <FeatureBadge label="Code Interpreter" active={service.config.codeInterpreter} onClick={() => update('codeInterpreter', !service.config.codeInterpreter)} icon={Terminal} />
             <FeatureBadge label="Reasoning (o1)" active={service.config.reasoning} onClick={() => update('reasoning', !service.config.reasoning)} icon={Brain} />
        </div>
    );
  };

  const renderClaudeControls = (service: Service) => {
    const update = (k: keyof ServiceConfig, v: any) => onUpdateService(service.id, { config: { ...service.config, [k]: v } });
    return (
         <div className="flex items-center gap-4 w-full">
             <FeatureBadge label="Artifacts" active={service.config.artifacts} onClick={() => update('artifacts', !service.config.artifacts)} icon={Layers} />
             <FeatureBadge label="Thinking" active={service.config.thinking} onClick={() => update('thinking', !service.config.thinking)} icon={Brain} />
        </div>
    );
  };

  // --- Main Render ---

  return (
    <div className="flex flex-col h-full bg-tower-900 relative">
      
      {/* 1. Header & Stats (Merged) */}
      <div className="bg-tower-800 border-b border-tower-600 px-6 py-3 flex items-center justify-between z-20 shadow-md">
         <div className="flex items-center gap-6">
            <h1 className="text-lg font-bold text-white tracking-widest flex items-center gap-2">
                <Shield className="text-tower-accent" size={20} /> TOWER
            </h1>
            <div className="h-6 w-px bg-tower-600" />
            <div className="flex gap-6 text-[10px] font-mono text-gray-400">
                <div className="flex flex-col">
                    <span className="text-gray-600">NODES</span>
                    <span className="text-white font-bold">{services.length}</span>
                </div>
                <div className="flex flex-col">
                    <span className="text-gray-600">ONLINE</span>
                    <span className="text-tower-success font-bold">{services.filter(s => s.status === 'online').length}</span>
                </div>
                <div className="flex flex-col">
                    <span className="text-gray-600">TOTAL REQ</span>
                    <span className="text-tower-accent font-bold">{(services.reduce((acc, s) => acc + s.stats.requests24h, 0)/1000).toFixed(1)}k</span>
                </div>
            </div>
         </div>

         <button 
            onClick={() => setIsModalOpen(true)}
            className="bg-tower-700 hover:bg-tower-600 border border-tower-600 text-white text-xs font-bold py-1.5 px-4 rounded transition-all flex items-center gap-2 shadow-sm"
         >
            <Shield size={14} className="text-tower-accent" /> ADD NODE
         </button>
      </div>

      {/* 2. Service Grid (Compact Rows) */}
      <div className="flex-1 overflow-y-auto p-4 space-y-3 bg-tower-900/50">
        {services.map(service => {
            const isOnline = service.status === 'online';
            return (
                <div 
                    key={service.id} 
                    className={`
                        relative bg-tower-800 border rounded-lg transition-all duration-300
                        ${service.isSelected ? 'border-tower-accent/50 shadow-[0_0_10px_rgba(0,240,255,0.05)]' : 'border-tower-700 hover:border-tower-600'}
                    `}
                >
                    {/* Row Main Content */}
                    <div className="flex items-center p-3 gap-4 h-20">
                        
                        {/* A. Selection Toggle */}
                        <div className="pl-2 pr-2 border-r border-tower-700/50 flex flex-col items-center justify-center gap-1">
                             <Toggle active={service.isSelected} onClick={() => handleToggleSelection(service.id)} disabled={service.loginStatus !== 'success'} />
                             <span className={`text-[9px] font-bold ${service.isSelected ? 'text-tower-accent' : 'text-gray-600'}`}>TARGET</span>
                        </div>

                        {/* B. Identity */}
                        <div className="w-48 flex-shrink-0">
                            <div className="flex items-center gap-2 mb-1">
                                <div className={`w-2 h-2 rounded-full ${isOnline ? 'bg-tower-success shadow-[0_0_5px_rgba(0,255,157,0.8)]' : 'bg-red-500'}`} />
                                <span className="font-bold text-gray-200 text-sm">{service.name}</span>
                            </div>
                            <div className="text-[10px] text-gray-500 font-mono truncate">{service.url}</div>
                            <div className="mt-1 flex gap-2">
                                <span className={`text-[9px] px-1.5 rounded ${service.loginStatus === 'success' ? 'bg-green-900/30 text-green-400' : 'bg-tower-700 text-gray-500'}`}>
                                    {service.loginStatus === 'success' ? 'LOGGED IN' : 'NO AUTH'}
                                </span>
                                {service.discoveryStatus === 'complete' && (
                                    <span className="text-[9px] px-1.5 rounded bg-purple-900/30 text-purple-400">MAPPED</span>
                                )}
                            </div>
                        </div>

                        {/* C. Feature Matrix (Dynamic) */}
                        <div className="flex-1 border-l border-r border-tower-700/50 px-4 h-full flex items-center bg-black/20">
                            {service.loginStatus !== 'success' ? (
                                <div className="flex items-center justify-between w-full">
                                    <span className="text-xs text-gray-500 italic flex items-center gap-2">
                                        <Lock size={12} /> Authentication required to access features
                                    </span>
                                    <button className="text-[10px] bg-tower-700 hover:bg-tower-600 text-white px-3 py-1 rounded" disabled>
                                        Retrying Login...
                                    </button>
                                </div>
                            ) : service.discoveryStatus !== 'complete' ? (
                                <div className="flex items-center justify-between w-full animate-pulse">
                                    <span className="text-xs text-purple-400 flex items-center gap-2">
                                        <Eye size={14} className="animate-spin" /> Scanning Interface Structure...
                                    </span>
                                    <button 
                                        onClick={() => onRunDiscovery(service.id)}
                                        className="text-[10px] bg-purple-600 hover:bg-purple-500 text-white px-3 py-1 rounded font-bold shadow-lg shadow-purple-500/20"
                                    >
                                        IDENTIFY FEATURES
                                    </button>
                                </div>
                            ) : (
                                <>
                                    {service.type === 'zai' && renderZaiControls(service)}
                                    {service.type === 'chatgpt' && renderChatGPTControls(service)}
                                    {service.type === 'claude' && renderClaudeControls(service)}
                                </>
                            )}
                        </div>

                        {/* D. Metrics & Expand */}
                        <div className="w-24 flex-shrink-0 flex flex-col items-end justify-center gap-1">
                            <span className="text-[10px] font-mono text-gray-500">LATENCY</span>
                            <span className="text-xs font-bold text-white">{service.stats.avgLatency}ms</span>
                            <button 
                                onClick={() => handleToggleExpand(service.id)}
                                className="mt-1 text-gray-500 hover:text-white transition-colors"
                            >
                                {service.expanded ? <ChevronDown size={16} className="rotate-180" /> : <ChevronDown size={16} />}
                            </button>
                        </div>
                    </div>

                    {/* Expanded Row Content */}
                    {service.expanded && (
                        <div className="border-t border-tower-700 bg-black/40 p-4 animate-fade-in flex gap-4">
                            <div className="flex-1">
                                <p className="text-[10px] font-mono text-gray-500 mb-2 uppercase">Last Server Response</p>
                                <div className="bg-tower-900 border border-tower-700 rounded p-3 font-mono text-xs text-gray-300 h-24 overflow-y-auto">
                                    {service.lastResponse || <span className="text-gray-600 italic">No interaction history.</span>}
                                </div>
                            </div>
                            <div className="w-64 border-l border-tower-700 pl-4">
                                <p className="text-[10px] font-mono text-gray-500 mb-2 uppercase">Connection Details</p>
                                <div className="space-y-1 text-xs">
                                    <div className="flex justify-between"><span className="text-gray-600">Port</span> <span className="text-tower-accent">{service.port}</span></div>
                                    <div className="flex justify-between"><span className="text-gray-600">Model</span> <span className="text-white">{service.config.model}</span></div>
                                    <div className="flex justify-between"><span className="text-gray-600">Cookies</span> <span className="text-green-400">Valid</span></div>
                                </div>
                            </div>
                        </div>
                    )}
                </div>
            );
        })}
      </div>

      {/* 3. Global Broadcast Bar */}
      <div className="bg-tower-800 border-t border-tower-600 p-4 z-30 shadow-[0_-5px_30px_rgba(0,0,0,0.5)]">
        <div className="max-w-7xl mx-auto flex items-center gap-4">
            <div className="bg-tower-700 rounded-lg px-3 py-2 flex flex-col items-center min-w-[80px]">
                <span className="text-xl font-bold text-tower-accent leading-none">
                    {services.filter(s => s.isSelected && s.status === 'online').length}
                </span>
                <span className="text-[8px] font-bold text-gray-400 tracking-wider">TARGETS</span>
            </div>

            <div className="flex-1 relative">
                <input 
                    type="text" 
                    value={globalInput}
                    onChange={(e) => setGlobalInput(e.target.value)}
                    onKeyDown={(e) => e.key === 'Enter' && handleGlobalSend()}
                    placeholder="Broadcast command to all selected nodes..."
                    className="w-full bg-tower-900 border border-tower-600 rounded-lg py-3 pl-4 pr-32 text-white focus:border-tower-accent focus:outline-none focus:ring-1 focus:ring-tower-accent transition-all font-mono text-sm"
                />
                <div className="absolute right-2 top-2 bottom-2 flex items-center gap-2">
                    <button className="text-gray-500 hover:text-white p-1"><Paperclip size={16} /></button>
                    <button 
                        onClick={handleGlobalSend}
                        className="bg-tower-accent hover:bg-cyan-300 text-black font-bold px-4 py-1.5 rounded text-xs flex items-center gap-1 transition-colors"
                    >
                        SEND <Send size={12} />
                    </button>
                </div>
            </div>
        </div>
      </div>

      {/* Add Service Modal */}
      {isModalOpen && (
        <div className="fixed inset-0 bg-black/80 backdrop-blur-sm flex items-center justify-center z-50">
          <div className="bg-tower-900 border border-tower-accent rounded-xl p-8 w-full max-w-lg shadow-2xl shadow-cyan-500/20 relative overflow-hidden">
            <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-transparent via-tower-accent to-transparent" />
            
            <h3 className="text-xl font-bold text-white mb-6 flex items-center gap-2">
              <Shield className="text-tower-accent" /> ADD NEW COMPUTE NODE
            </h3>
            
            <form onSubmit={handleAdd} className="space-y-4">
              <div className="bg-tower-800 p-4 rounded-lg border border-tower-700">
                  <label className="block text-[10px] font-mono text-gray-500 mb-1 uppercase tracking-wider">Service Endpoint URL</label>
                  <div className="flex items-center bg-tower-900 rounded border border-tower-600 px-3">
                    <Globe size={14} className="text-gray-500 mr-2" />
                    <input 
                        type="url" required 
                        placeholder="https://chat.openai.com"
                        className="w-full bg-transparent py-2 text-white focus:outline-none text-sm"
                        value={newUrl} onChange={e => setNewUrl(e.target.value)}
                    />
                  </div>
              </div>

              <div className="grid grid-cols-2 gap-4">
                <div>
                    <label className="block text-[10px] font-mono text-gray-500 mb-1 uppercase tracking-wider">Username / Email</label>
                    <input 
                        type="text" required 
                        placeholder="agent@tower.ai"
                        className="w-full bg-tower-800 border border-tower-600 rounded p-2 text-white text-sm focus:border-tower-accent focus:outline-none"
                        value={newEmail} onChange={e => setNewEmail(e.target.value)}
                    />
                </div>
                <div>
                    <label className="block text-[10px] font-mono text-gray-500 mb-1 uppercase tracking-wider">Password</label>
                    <input 
                        type="password" required 
                        placeholder="••••••••••••"
                        className="w-full bg-tower-800 border border-tower-600 rounded p-2 text-white text-sm focus:border-tower-accent focus:outline-none"
                        value={newPass} onChange={e => setNewPass(e.target.value)}
                    />
                </div>
              </div>

              <div className="bg-blue-900/20 border border-blue-500/30 p-3 rounded text-xs text-blue-300 flex items-start gap-2">
                <CheckCircle size={14} className="mt-0.5" />
                <p>System will automatically attempt to login, solve captchas, and capture session cookies. Visual inspection will follow upon success.</p>
              </div>

              <div className="flex gap-3 mt-6">
                <button type="button" onClick={() => setIsModalOpen(false)} className="flex-1 py-3 bg-tower-800 hover:bg-tower-700 text-gray-400 rounded-lg font-bold text-xs">CANCEL</button>
                <button type="submit" className="flex-1 py-3 bg-tower-accent hover:bg-cyan-300 text-black rounded-lg font-bold text-xs tracking-wide">INITIALIZE SEQUENCE</button>
              </div>
            </form>
          </div>
        </div>
      )}

    </div>
  );
};

export default ServiceManager;