export type ServiceType = 'zai' | 'chatgpt' | 'claude' | 'generic';

export type ZaiMode = 'general' | 'deep-research' | 'magic-design' | 'full-stack' | 'ai-slides' | 'write-code';

export interface ServiceConfig {
  // Common
  model: string;
  
  // Z.ai Specific
  deepThink?: boolean;
  search?: boolean;
  advancedSearch?: boolean;
  mode?: ZaiMode;
  
  // ChatGPT Specific
  webBrowsing?: boolean;
  dalle?: boolean;
  codeInterpreter?: boolean;
  reasoning?: boolean; // o1-preview behavior
  
  // Claude Specific
  artifacts?: boolean;
  thinking?: boolean;
}

export interface ServiceFlow {
  id: string;
  name: string;
  description: string;
  status: 'active' | 'broken' | 'testing';
  steps: string[];
}

export interface Service {
  id: string;
  type: ServiceType;
  name: string;
  url: string;
  port: number;
  
  // Lifecycle State
  status: 'online' | 'offline' | 'maintenance' | 'analyzing';
  loginStatus: 'pending' | 'processing' | 'success' | 'failed';
  discoveryStatus: 'pending' | 'scanning' | 'complete';
  authType: 'login/pass' | 'oauth' | 'cookie';
  
  lastPing: number;
  stats: {
    uptime: number;
    requests24h: number;
    avgLatency: number;
  };
  
  // Configuration
  config: ServiceConfig;
  availableModels: string[];
  
  // Selection & UI
  isSelected: boolean; 
  lastResponse?: string;
  expanded?: boolean;

  // Automation
  flows: ServiceFlow[];
}

export interface LogEntry {
  id: string;
  timestamp: string;
  level: 'info' | 'warn' | 'error' | 'vision';
  serviceId?: string;
  message: string;
}

export interface QueueItem {
  id: string;
  serviceId: string;
  payload: string;
  status: 'pending' | 'processing' | 'completed';
  timestamp: number;
}