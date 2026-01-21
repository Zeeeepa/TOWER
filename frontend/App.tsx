import React, { useState, useEffect } from 'react';
import ServiceManager from './components/ServiceManager';
import { Service, LogEntry, QueueItem, ServiceType } from './types';
import { generateSimulationResponse } from './services/geminiService';

const App: React.FC = () => {
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [queue, setQueue] = useState<QueueItem[]>([]);

  // Load services from localStorage on mount
  const getInitialServices = (): Service[] => {
    const saved = localStorage.getItem('tower_services');
    if (saved) {
      try {
        return JSON.parse(saved);
      } catch (e) {
        console.error('Failed to parse saved services:', e);
      }
    }

    // Default services if nothing saved
    return [
    {
        id: '1',
        type: 'chatgpt',
        name: 'OpenAI ChatGPT',
        url: 'https://chat.openai.com',
        port: 3001,
        status: 'online',
        loginStatus: 'success',
        discoveryStatus: 'complete',
        authType: 'login/pass',
        lastPing: Date.now(),
        flows: [],
        stats: { uptime: 99.9, requests24h: 4500, avgLatency: 800 },
        availableModels: ['GPT-4', 'GPT-4o'],
        config: {
            model: 'GPT-4o',
            webBrowsing: true,
            dalle: false,
            codeInterpreter: true,
            reasoning: false
        },
        isSelected: true,
        expanded: false
    },
    {
        id: '2',
        type: 'zai',
        name: 'Z.AI Agent',
        url: 'https://z.ai',
        port: 3002,
        status: 'online',
        loginStatus: 'success',
        discoveryStatus: 'complete',
        authType: 'cookie',
        lastPing: Date.now(),
        flows: [],
        stats: { uptime: 99.5, requests24h: 2100, avgLatency: 1200 },
        availableModels: ['Z-Ultra'],
        config: {
            model: 'Z-Ultra',
            deepThink: true,
            search: true,
            advancedSearch: true,
            mode: 'deep-research'
        },
        isSelected: true,
        expanded: false
    },
    {
        id: '3',
        type: 'claude',
        name: 'Anthropic Claude',
        url: 'https://claude.ai',
        port: 3003,
        status: 'online',
        loginStatus: 'success',
        discoveryStatus: 'complete',
        authType: 'login/pass',
        lastPing: Date.now(),
        flows: [],
        stats: { uptime: 99.0, requests24h: 1500, avgLatency: 600 },
        availableModels: ['Claude 3.5 Sonnet'],
        config: {
            model: 'Claude 3.5 Sonnet',
            artifacts: true,
            thinking: false
        },
        isSelected: false,
        expanded: false
    }
  ];
  };

  const [services, setServices] = useState<Service[]>(getInitialServices());

  // Persist services to localStorage whenever they change
  useEffect(() => {
    localStorage.setItem('tower_services', JSON.stringify(services));
  }, [services]);

  const addLog = (level: LogEntry['level'], message: string, serviceId?: string) => {
    // In a real app this would go to a global store or context
    console.log(`[${level.toUpperCase()}] ${message}`);
  };

  const handleUpdateService = (id: string, updates: Partial<Service>) => {
    setServices(prev => prev.map(s => s.id === id ? { ...s, ...updates } : s));
  };

  const handleSendMessage = async (serviceId: string, payload: any) => {
    const service = services.find(s => s.id === serviceId);
    if (!service) return;

    addLog('info', `Sending to ${service.name}...`, serviceId);
    
    // Simulate Response
    const responseText = await generateSimulationResponse(
        `Simulate ${service.name} response. Prompt: "${payload.prompt}". Features: ${JSON.stringify(payload.configuration.features)}. Keep short.`
    );

    handleUpdateService(serviceId, { lastResponse: responseText });
  };

  const handleAddService = async (url: string, email: string, pass: string) => {
    const newId = Math.random().toString(36).substr(2, 9);
    
    // Determine type based on URL (simple heuristic)
    let type: ServiceType = 'generic';
    if (url.includes('openai')) type = 'chatgpt';
    else if (url.includes('z.ai')) type = 'zai';
    else if (url.includes('claude')) type = 'claude';

    const newService: Service = {
      id: newId,
      type,
      name: new URL(url).hostname.replace('www.', '').split('.')[0].toUpperCase() + ' (Node)',
      url,
      port: 3000 + services.length + 1,
      status: 'analyzing',
      loginStatus: 'processing', // Start logging in immediately
      discoveryStatus: 'pending',
      authType: 'login/pass',
      lastPing: Date.now(),
      flows: [],
      stats: { uptime: 100, requests24h: 0, avgLatency: 0 },
      availableModels: ['Default'],
      config: { model: 'Default' },
      isSelected: false,
      expanded: true
    };

    setServices(prev => [...prev, newService]);

    // Simulate Login Process
    setTimeout(() => {
        handleUpdateService(newId, { loginStatus: 'success', status: 'online' });
        // After login, waiting for user to click "Identify Features"
    }, 2500);
  };

  const handleRunDiscovery = (id: string) => {
    handleUpdateService(id, { discoveryStatus: 'scanning' });
    
    // Simulate Vision Analysis
    setTimeout(() => {
        const s = services.find(serv => serv.id === id);
        let defaultConfig: any = { model: 'Default' };
        
        // Populate features based on type
        if (s?.type === 'zai') {
             defaultConfig = { deepThink: false, search: false, advancedSearch: false, mode: 'general' };
        } else if (s?.type === 'chatgpt') {
             defaultConfig = { webBrowsing: true, dalle: true };
        } else if (s?.type === 'claude') {
             defaultConfig = { artifacts: true };
        }

        handleUpdateService(id, { 
            discoveryStatus: 'complete', 
            config: { ...s?.config, ...defaultConfig },
            isSelected: true 
        });
    }, 3000);
  };

  const handleToggleStatus = (id: string) => {
    setServices(prev => prev.map(s => {
        if(s.id === id) {
            return { ...s, status: s.status === 'online' ? 'offline' : 'online' };
        }
        return s;
    }));
  };

  return (
    <div className="flex h-screen bg-tower-900 text-gray-200 font-sans overflow-hidden">
        <div className="flex-1 flex flex-col relative">
            <div className="absolute inset-0 bg-[url('https://grainy-gradients.vercel.app/noise.svg')] opacity-5 pointer-events-none"></div>
            <ServiceManager 
                services={services} 
                onAddService={handleAddService}
                onSelectService={() => {}}
                onToggleStatus={handleToggleStatus}
                onUpdateService={handleUpdateService}
                onSendMessage={handleSendMessage}
                onRunDiscovery={handleRunDiscovery}
            />
        </div>
    </div>
  );
};

export default App;